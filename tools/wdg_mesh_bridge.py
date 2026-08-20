#!/usr/bin/env python3
"""Configure a serial-only Heltec MeshCore sidecar and relay live GPS adverts."""

from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
from getpass import getpass
import hashlib
import hmac
import json
import os
from pathlib import Path
import platform
import re
import secrets
import sys
import time
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


VERSION = "0.1.0-beta.1"
PROTOCOL = "WDG1"
WDG_ME_URL = "https://wdgwars.pl/api/me"
WDG_UPLOAD_URL = "https://wdgwars.pl/api/upload/"
MESHCORE_CONFIG_URL = "https://api.meshcore.nz/api/v1/config"
KEYRING_SERVICE = "Canadaverse WDG Mesh Serial Bridge"
KEYRING_ACCOUNT = "default"
KEY_RE = re.compile(r"^[0-9a-fA-F]{64}$")
CREDENTIAL_RE = re.compile(r"(?i)(?<![0-9a-f])[0-9a-f]{64}(?![0-9a-f])")
NODE_RE = re.compile(r"^[0-9a-fA-F]{8}$")
HELLO_RE = re.compile(r"^WDG1 HELLO 1 (.+) (\S+)$")
CONFIG_VERSION = 1


class BridgeError(RuntimeError):
    """Expected setup, serial, credential, or network failure."""


class AuthenticationError(BridgeError):
    """WDG explicitly rejected the API credential or signature."""


def user_agent() -> str:
    return (
        f"WatchDogsGo/2.0 CanadaverseWDGMeshBridge/{VERSION} "
        f"({platform.system()}; Python/{sys.version_info.major}.{sys.version_info.minor})"
    )


def config_path() -> Path:
    if os.name == "nt":
        root = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
    else:
        root = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return root / "canadaverse-wdg-mesh-serial" / "config.json"


def load_config() -> dict[str, Any]:
    path = config_path()
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise BridgeError("not configured; run 'configure' first") from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise BridgeError(f"could not read {path}: {exc}") from exc
    if value.get("version") != CONFIG_VERSION:
        raise BridgeError("unsupported configuration; run 'configure' again")
    if any("key" in key.lower() for key in value):
        raise BridgeError("unsafe configuration contains a key-like field")
    if CREDENTIAL_RE.search(json.dumps(value)):
        raise BridgeError("unsafe configuration contains a credential-shaped value")
    return value


def save_config(value: dict[str, Any]) -> None:
    encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if CREDENTIAL_RE.search(encoded):
        raise BridgeError("refusing to write a credential-shaped value to config")
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(encoded, encoding="utf-8")
    try:
        os.chmod(temporary, 0o600)
    except OSError:
        pass
    temporary.replace(path)


def keyring_module():
    try:
        import keyring
        from keyring.errors import KeyringError
    except ImportError as exc:
        raise BridgeError("missing dependency; run setup_bridge.py") from exc
    return keyring, KeyringError


def store_api_key(api_key: str) -> None:
    keyring, KeyringError = keyring_module()
    try:
        keyring.set_password(KEYRING_SERVICE, KEYRING_ACCOUNT, api_key)
        if keyring.get_password(KEYRING_SERVICE, KEYRING_ACCOUNT) != api_key:
            raise BridgeError("OS credential vault did not return the stored key")
    except (KeyringError, RuntimeError) as exc:
        raise BridgeError(
            "no usable OS credential vault; configure Windows Credential Manager, "
            "Secret Service, or provide WDG_API_KEY only in the bridge process environment"
        ) from exc


def load_api_key() -> str:
    candidate = os.environ.get("WDG_API_KEY", "")
    if not candidate:
        keyring, KeyringError = keyring_module()
        try:
            candidate = keyring.get_password(KEYRING_SERVICE, KEYRING_ACCOUNT) or ""
        except KeyringError as exc:
            raise BridgeError(f"could not read the OS credential vault: {exc}") from exc
    if not KEY_RE.fullmatch(candidate):
        raise BridgeError("WDG API key is missing or is not 64 hexadecimal characters")
    return candidate.lower()


def forget_api_key() -> None:
    keyring, KeyringError = keyring_module()
    try:
        keyring.delete_password(KEYRING_SERVICE, KEYRING_ACCOUNT)
    except KeyringError as exc:
        if "not found" not in str(exc).lower():
            raise BridgeError(f"could not remove credential: {exc}") from exc


def request_json(request: Request, timeout: float = 30) -> dict[str, Any]:
    try:
        with urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")[:200]
        if exc.code in (401, 403):
            raise AuthenticationError("WDG rejected the API key or request signature") from exc
        raise BridgeError(f"HTTP {exc.code}: {detail}") from exc
    except (URLError, TimeoutError, OSError, json.JSONDecodeError) as exc:
        raise BridgeError(f"network request was not confirmed: {exc}") from exc


def validate_api_key(api_key: str) -> dict[str, Any]:
    request = Request(WDG_ME_URL, method="GET")
    request.add_header("Accept", "application/json")
    request.add_header("X-API-Key", api_key)
    request.add_header("User-Agent", user_agent())
    result = request_json(request)
    if result.get("ok") is not True:
        raise BridgeError("WDG authentication did not return ok=true")
    return result


def sign_payload(payload: dict[str, Any], api_key: str, nonce: str | None = None) -> bytes:
    raw = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    encoded = base64.b64encode(raw).decode("ascii")
    nonce = nonce or secrets.token_hex(8)
    signature = hmac.new(
        api_key.encode("ascii"),
        (nonce + encoded).encode("ascii"),
        hashlib.sha256,
    ).hexdigest()
    return json.dumps(
        {"data": encoded, "nonce": nonce, "sig": signature},
        separators=(",", ":"),
    ).encode("utf-8")


def upload(nodes: list[dict[str, Any]], api_key: str) -> dict[str, Any]:
    payload = {"networks": [], "aircraft": [], "meshcore_nodes": nodes}
    request = Request(WDG_UPLOAD_URL, data=sign_payload(payload, api_key), method="POST")
    request.add_header("Content-Type", "application/json")
    request.add_header("Accept", "application/json")
    request.add_header("X-API-Key", api_key)
    request.add_header("User-Agent", user_agent())
    return request_json(request)


def fetch_regions() -> list[dict[str, Any]]:
    request = Request(MESHCORE_CONFIG_URL, method="GET")
    request.add_header("Accept", "application/json")
    request.add_header("User-Agent", user_agent())
    result = request_json(request)
    try:
        entries = result["config"]["suggested_radio_settings"]["entries"]
    except (KeyError, TypeError) as exc:
        raise BridgeError("official MeshCore region response changed format") from exc
    regions: list[dict[str, Any]] = []
    for entry in entries:
        try:
            region = {
                "title": str(entry["title"]),
                "frequency": float(entry["frequency"]),
                "bandwidth": float(entry["bandwidth"]),
                "spreading_factor": int(entry["spreading_factor"]),
                "coding_rate": int(entry["coding_rate"]),
            }
        except (KeyError, TypeError, ValueError):
            continue
        if valid_region(region):
            region["slug"] = slugify(region["title"])
            regions.append(region)
    if not regions:
        raise BridgeError("official MeshCore service returned no usable regions")
    return regions


def valid_region(region: dict[str, Any]) -> bool:
    return (
        150 <= region["frequency"] <= 960
        and 7 <= region["bandwidth"] <= 500
        and 5 <= region["spreading_factor"] <= 12
        and 5 <= region["coding_rate"] <= 8
    )


def slugify(title: str) -> str:
    value = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
    return value[:20] or "meshcore"


def serial_modules():
    try:
        import serial
        from serial.tools import list_ports
    except ImportError as exc:
        raise BridgeError("missing dependency; run setup_bridge.py") from exc
    return serial, list_ports


def port_identity(port: Any) -> dict[str, Any]:
    return {
        "vid": port.vid,
        "pid": port.pid,
        "serial_number": port.serial_number or None,
        "location": port.location or None,
        "manufacturer": port.manufacturer or None,
        "product": port.product or None,
        "port_hint": port.device,
    }


def choose_port(port_name: str | None = None) -> tuple[str, dict[str, Any]]:
    _, list_ports = serial_modules()
    ports = list(list_ports.comports())
    if not ports:
        raise BridgeError("no serial devices are attached")
    if port_name:
        matches = [port for port in ports if port.device.casefold() == port_name.casefold()]
        if len(matches) != 1:
            raise BridgeError(f"serial port {port_name!r} is not attached")
        selected = matches[0]
        print(f"Using explicitly selected sidecar {selected.device}.")
        return selected.device, port_identity(selected)
    print("\nAttached serial devices:")
    for index, port in enumerate(ports, 1):
        label = " ".join(filter(None, (port.product, port.manufacturer))) or "serial device"
        usb = f"{port.vid:04X}:{port.pid:04X}" if port.vid is not None else "non-USB"
        print(f"  {index}. {port.device}  {usb}  {label}")
    while True:
        answer = input("Select the flashed WDG Mesh Sidecar: ").strip()
        try:
            selected = ports[int(answer) - 1]
            return selected.device, port_identity(selected)
        except (ValueError, IndexError):
            print("Enter one of the numbers shown above.")


def resolve_port(identity: dict[str, Any]) -> str:
    _, list_ports = serial_modules()
    ports = list(list_ports.comports())
    matches = [
        port
        for port in ports
        if port.vid == identity.get("vid") and port.pid == identity.get("pid")
    ]
    serial_number = identity.get("serial_number")
    location = identity.get("location")
    if serial_number:
        matches = [port for port in matches if port.serial_number == serial_number]
    elif location:
        matches = [port for port in matches if port.location == location]
    elif identity.get("port_hint"):
        matches = [port for port in matches if port.device == identity["port_hint"]]
    if len(matches) != 1:
        found = ", ".join(port.device for port in matches) or "none"
        raise BridgeError(
            f"configured sidecar identity matched {len(matches)} ports ({found}); "
            "reconnect it and run 'configure'"
        )
    return matches[0].device


def open_device(port: str):
    serial, _ = serial_modules()
    try:
        device = serial.Serial(port, 115200, timeout=0.25, write_timeout=2)
        device.dtr = False
        device.rts = False
        return device
    except (serial.SerialException, OSError) as exc:
        raise BridgeError(f"could not open {port}: {exc}") from exc


def write_line(device: Any, text: str) -> None:
    try:
        device.write((text + "\n").encode("ascii"))
        device.flush()
    except OSError as exc:
        raise BridgeError(f"serial write failed: {exc}") from exc


def read_line(device: Any) -> str:
    try:
        return device.readline(2048).decode("utf-8", errors="replace").strip()
    except OSError as exc:
        raise BridgeError(f"serial read failed: {exc}") from exc


def probe_device(device: Any, timeout: float = 10) -> tuple[str, str]:
    deadline = time.monotonic() + timeout
    next_hello = 0.0
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now >= next_hello:
            write_line(device, "WDG1 HELLO")
            next_hello = now + 0.75
        line = read_line(device)
        match = HELLO_RE.match(line)
        if match:
            return match.group(1), match.group(2)
    raise BridgeError("selected device did not answer the WDG1 serial handshake")


def start_command(region: dict[str, Any]) -> str:
    return (
        f"WDG1 START {region['frequency']:.3f} {region['bandwidth']:g} "
        f"{region['spreading_factor']} {region['coding_rate']} "
        f"{int(time.time())} {region['slug']}"
    )


def apply_region(device: Any, region: dict[str, Any], timeout: float = 5) -> None:
    write_line(device, "WDG1 AUTH OK")
    write_line(device, start_command(region))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = read_line(device)
        if line.startswith("WDG1 READY 1 "):
            return
        if line.startswith("WDG1 ERROR "):
            raise BridgeError(f"device rejected region: {line}")
    raise BridgeError("device did not confirm the selected MeshCore region")


def choose_region(
    regions: list[dict[str, Any]], region_name: str | None = None
) -> dict[str, Any]:
    if region_name:
        requested_slug = slugify(region_name)
        matches = [
            region
            for region in regions
            if region["title"].casefold() == region_name.casefold()
            or region["slug"].casefold() == region_name.casefold()
            or region["slug"] == requested_slug
        ]
        if len(matches) != 1:
            raise BridgeError(f"official MeshCore region {region_name!r} was not found")
        print(f"Using explicitly selected region {matches[0]['title']}.")
        return matches[0]
    print("\nOfficial MeshCore radio presets:")
    recommended = 0
    for index, region in enumerate(regions, 1):
        if region["title"] == "USA/Canada (Recommended)":
            recommended = index
        print(
            f"  {index}. {region['title']} — {region['frequency']:.3f} MHz, "
            f"SF{region['spreading_factor']}, BW{region['bandwidth']:g}, "
            f"CR4/{region['coding_rate']}"
        )
    prompt = f"Select region [{recommended}]: " if recommended else "Select region: "
    while True:
        answer = input(prompt).strip()
        if not answer and recommended:
            answer = str(recommended)
        try:
            return regions[int(answer) - 1]
        except (ValueError, IndexError):
            print("Enter one of the numbers shown above.")


def configure(port_name: str | None = None, region_name: str | None = None) -> int:
    print("Canadaverse WDG Mesh Sidecar — serial-only setup")
    port, identity = choose_port(port_name)
    region = choose_region(fetch_regions(), region_name)
    api_key = getpass("WDG API key (hidden): ").strip()
    if not KEY_RE.fullmatch(api_key):
        raise BridgeError("WDG API key must be exactly 64 hexadecimal characters")
    print("Checking WDG authentication...")
    validate_api_key(api_key)
    print(f"Checking sidecar on {port}...")
    with open_device(port) as device:
        board, firmware = probe_device(device)
        apply_region(device, region)
        write_line(device, "WDG1 STOP")
    store_api_key(api_key.lower())
    save_config(
        {
            "version": CONFIG_VERSION,
            "device": identity,
            "region": region,
        }
    )
    print(f"Configured {board} ({firmware}) for {region['title']}.")
    print("The API key is in the OS credential vault, not the config file or device.")
    print("Run: python tools/wdg_mesh_bridge.py run")
    return 0


def parse_record(line: str, received_at: datetime | None = None) -> dict[str, Any] | None:
    if not line.startswith("WDG1 {"):
        return None
    try:
        raw = json.loads(line[len("WDG1 ") :])
        node_id = str(raw["node_id"]).lower()
        latitude = float(raw["lat"])
        longitude = float(raw["lon"])
        rssi = float(raw.get("rssi", 0))
    except (KeyError, TypeError, ValueError, OverflowError, json.JSONDecodeError):
        return None
    if not NODE_RE.fullmatch(node_id):
        return None
    if not (-90 <= latitude <= 90 and -180 <= longitude <= 180):
        return None
    if latitude == 0 and longitude == 0:
        return None
    observed = (received_at or datetime.now(timezone.utc)).strftime("%Y-%m-%dT%H:%M:%S")
    return {
        "node_id": node_id,
        "node_type": str(raw.get("node_type", "Unknown"))[:16],
        "name": str(raw.get("name", ""))[:32],
        "lat": latitude,
        "lon": longitude,
        "rssi": rssi,
        "first_seen": observed,
        "type": "MESHCORE",
    }


def flush_batch(
    device: Any,
    pending: dict[str, dict[str, Any]],
    attempted: set[str],
    api_key: str,
    dry_run: bool,
) -> None:
    nodes = list(pending.values())
    if not nodes:
        return
    # A missing HTTP response may still mean accepted. Consume before POST and
    # never replay automatically; a future process sees only future live RF.
    attempted.update(pending)
    pending.clear()
    if dry_run:
        print(json.dumps({"meshcore_nodes": nodes}, indent=2))
        return
    try:
        result = upload(nodes, api_key)
        imported = int(result.get("meshcore_imported", 0))
        print(f"WDG accepted batch: sent={len(nodes)} imported={imported}")
        write_line(device, f"WDG1 ACK {len(nodes)} {imported}")
    except AuthenticationError:
        try:
            write_line(device, "WDG1 AUTH FAIL")
        except BridgeError:
            pass
        raise
    except BridgeError as exc:
        print(f"WDG upload not confirmed: {exc}", file=sys.stderr)
        try:
            write_line(device, "WDG1 SEND UNCONFIRMED")
        except BridgeError:
            pass


def run_bridge(dry_run: bool = False) -> int:
    config = load_config()
    region = config["region"]
    if not valid_region(region):
        raise BridgeError("saved radio preset is invalid; run 'configure' again")
    api_key = load_api_key()
    validate_api_key(api_key)
    pending: dict[str, dict[str, Any]] = {}
    attempted: set[str] = set()
    first_queued = 0.0
    last_queued = 0.0

    print(f"WDG authenticated. Listening on {region['title']}; Ctrl+C stops.")
    while True:
        try:
            port = resolve_port(config["device"])
            with open_device(port) as device:
                board, firmware = probe_device(device)
                print(f"Connected to {board} ({firmware}) on {port}.")
                write_line(device, "WDG1 AUTH OK")
                next_heartbeat = 0.0
                while True:
                    now = time.monotonic()
                    if now >= next_heartbeat:
                        write_line(device, start_command(region))
                        next_heartbeat = now + 5
                    line = read_line(device)
                    if line.startswith("WDG1 ERROR "):
                        raise BridgeError(line)
                    record = parse_record(line)
                    if record and record["node_id"] not in attempted:
                        was_empty = not pending
                        pending[record["node_id"]] = record
                        last_queued = now
                        if was_empty:
                            first_queued = now
                        print(
                            f"MeshCore {record['node_id']} {record['node_type']} "
                            f"{record['name'] or '(unnamed)'}"
                        )
                    if pending and (
                        len(pending) >= 16
                        or now - last_queued >= 15
                        or now - first_queued >= 60
                    ):
                        flush_batch(device, pending, attempted, api_key, dry_run)
                        first_queued = last_queued = 0.0
        except KeyboardInterrupt:
            try:
                write_line(device, "WDG1 STOP")
            except (BridgeError, UnboundLocalError):
                pass
            print("Stopped.")
            return 0
        except AuthenticationError:
            raise
        except BridgeError as exc:
            pending.clear()
            first_queued = last_queued = 0.0
            print(f"Serial link lost: {exc}; retrying in 2 seconds...", file=sys.stderr)
            time.sleep(2)


def status() -> int:
    config = load_config()
    region = config["region"]
    try:
        load_api_key()
        credential = "available in OS vault/environment"
    except BridgeError:
        credential = "missing"
    print(f"Config: {config_path()}")
    print(f"Region: {region['title']}")
    print(f"Credential: {credential}")
    try:
        print(f"Sidecar: {resolve_port(config['device'])}")
    except BridgeError as exc:
        print(f"Sidecar: unavailable ({exc})")
    return 0


def self_test() -> int:
    received = datetime(2026, 8, 12, 20, 0, 0, tzinfo=timezone.utc)
    record = parse_record(
        'WDG1 {"node_id":"01020304","node_type":"Repeater",'
        '"name":"test-node","lat":43.6532,"lon":-79.3832,"rssi":-92}',
        received,
    )
    assert record is not None and record["first_seen"] == "2026-08-12T20:00:00"
    payload = {"networks": [], "aircraft": [], "meshcore_nodes": [record]}
    envelope = json.loads(sign_payload(payload, "ab" * 32, "0123456789abcdef"))
    assert envelope["sig"] == "e9cac2e307c34081feeacd6a69a83edf03ec1578ee57062f49f7eba14026e62d"
    assert parse_record('WDG1 {"node_id":"bad"}', received) is None
    assert "WDG_API_KEY" not in json.dumps(
        {"version": CONFIG_VERSION, "device": {}, "region": {}}
    )
    print("Serial bridge self-test passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Relay live geolocated MeshCore adverts from a USB sidecar to WDG."
    )
    commands = parser.add_subparsers(dest="command", required=True)
    configure_parser = commands.add_parser(
        "configure", help="select a sidecar/region and store the key safely"
    )
    configure_parser.add_argument(
        "--port", help="explicit non-secret serial port, for example COM21 or /dev/ttyACM0"
    )
    configure_parser.add_argument(
        "--region", help="exact official MeshCore region title or slug"
    )
    run_parser = commands.add_parser("run", help="run the live serial-to-WDG bridge")
    run_parser.add_argument(
        "--dry-run", action="store_true", help="show live batches without POSTing"
    )
    commands.add_parser("status", help="show safe configuration status")
    commands.add_parser("forget-key", help="remove the key from the OS credential vault")
    commands.add_parser("self-test", help="test parsing and WDG signing offline")
    args = parser.parse_args()
    try:
        if args.command == "configure":
            return configure(args.port, args.region)
        if args.command == "run":
            return run_bridge(args.dry_run)
        if args.command == "status":
            return status()
        if args.command == "forget-key":
            forget_api_key()
            print("Credential removed.")
            return 0
        return self_test()
    except BridgeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
