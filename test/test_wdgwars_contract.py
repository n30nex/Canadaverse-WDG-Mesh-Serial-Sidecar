import base64
from datetime import datetime, timezone
import hashlib
import hmac
import importlib.util
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
BRIDGE_SOURCE = (ROOT / "tools" / "wdg_mesh_bridge.py").read_text(encoding="utf-8")
SETUP_SOURCE = (ROOT / "setup_bridge.py").read_text(encoding="utf-8")
BOARD_SOURCE = (ROOT / "src" / "boards" / "BoardSupport.cpp").read_text(encoding="utf-8")
BOARD_HEADER = (ROOT / "src" / "boards" / "BoardSupport.h").read_text(encoding="utf-8")
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")
BUILD_SCRIPT = (ROOT / "scripts" / "meshcore_ed25519.py").read_text(encoding="utf-8")
PACKAGE_SCRIPT = (ROOT / "scripts" / "package_firmware.py").read_text(encoding="utf-8")
NV_DISPLAY = (ROOT / "src" / "helpers" / "ui" / "NV3001BDisplay.cpp").read_text(encoding="utf-8")
CAPTURE_TOOL = (ROOT / "tools" / "capture_screen.py").read_text(encoding="utf-8")
WINDOWS_BACKGROUND = (ROOT / "tools" / "windows_background.ps1").read_text(encoding="utf-8")
WINDOWS_TRAY_HOST = (ROOT / "tools" / "windows_tray_host.vbs").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")

SPEC = importlib.util.spec_from_file_location("wdg_mesh_bridge", ROOT / "tools" / "wdg_mesh_bridge.py")
assert SPEC and SPEC.loader
BRIDGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BRIDGE)


def test_hmac_envelope_matches_watchdogsgo_algorithm():
    node = {
        "node_id": "01020304",
        "node_type": "Repeater",
        "name": "test-node",
        "lat": 43.6532,
        "lon": -79.3832,
        "rssi": -92.0,
        "first_seen": "2026-08-12T20:00:00",
        "type": "MESHCORE",
    }
    payload = {"networks": [], "aircraft": [], "meshcore_nodes": [node]}
    key = "ab" * 32
    nonce = "0123456789abcdef"
    envelope = json.loads(BRIDGE.sign_payload(payload, key, nonce))
    raw = json.dumps(payload, separators=(",", ":")).encode()
    encoded = base64.b64encode(raw).decode()
    expected = hmac.new(key.encode(), (nonce + encoded).encode(), hashlib.sha256).hexdigest()

    assert envelope == {"data": encoded, "nonce": nonce, "sig": expected}
    assert expected == "e9cac2e307c34081feeacd6a69a83edf03ec1578ee57062f49f7eba14026e62d"


def test_serial_record_becomes_live_meshcore_contract_with_host_receipt_time():
    received = datetime(2026, 8, 12, 20, 0, 0, tzinfo=timezone.utc)
    record = BRIDGE.parse_record(
        'WDG1 {"node_id":"01020304","node_type":"Repeater",'
        '"name":"test-node","lat":43.6532,"lon":-79.3832,"rssi":-92,'
        '"advert_timestamp":1}',
        received,
    )
    assert record == {
        "node_id": "01020304",
        "node_type": "Repeater",
        "name": "test-node",
        "lat": 43.6532,
        "lon": -79.3832,
        "rssi": -92.0,
        "first_seen": "2026-08-12T20:00:00",
        "type": "MESHCORE",
    }
    assert BRIDGE.parse_record('WDG1 {"node_id":"01020304","lat":0,"lon":0}', received) is None
    assert BRIDGE.parse_record('WDG1 {"node_id":"bad","lat":1,"lon":1}', received) is None


def test_full_canada_slug_selects_the_bounded_official_region_slug():
    region = {
        "title": "USA/Canada (Recommended)",
        "slug": BRIDGE.slugify("USA/Canada (Recommended)"),
        "frequency": 910.525,
    }
    assert BRIDGE.choose_region([region], "usa-canada-recommended") is region


def test_device_is_serial_only_and_has_no_credential_or_network_stack():
    for token in (
        "#include <WiFi.h>",
        "#include <NimBLEDevice.h>",
        "#include <HTTPClient.h>",
        "#include <WiFiClientSecure.h>",
        "WebServer",
        "DNSServer",
        "Preferences",
        "X-API-Key",
        "wdgwars.pl",
    ):
        assert token not in SOURCE
    assert re.findall(r'"([A-Fa-f0-9]{64})"', SOURCE) == []
    assert "h2zero/NimBLE-Arduino" not in PLATFORMIO
    assert "bleak" not in (ROOT / "requirements.txt").read_text(encoding="utf-8").lower()


def test_radio_is_passive_and_repeater_polling_is_absent():
    assert "bool startSendRaw(const uint8_t *, int) override { return false; }" in SOURCE
    assert "bool allowPacketForward(const mesh::Packet *) override { return false; }" in SOURCE
    for token in ("startTransmit(", "sendLogin(", "sendRequest("):
        assert token not in SOURCE
    assert "PASSIVE MESHCORE RX" in SOURCE


def test_only_verified_gps_adverts_leave_the_device():
    assert "BaseChatMesh invokes this callback only after the signed MeshCore advert" in SOURCE
    assert "if (!hasLocation || !hostReady())" in SOURCE
    assert 'record = F("WDG1 {\\\"node_id\\\":\\\"")' in SOURCE
    assert "contact.gps_lat >= -90000000" in SOURCE
    assert "contact.gps_lon >= -180000000" in SOURCE
    assert "(contact.gps_lat != 0 || contact.gps_lon != 0)" in SOURCE


def test_serial_lease_and_dynamic_official_region_settings_are_bounded():
    assert "constexpr uint32_t kHostLeaseMs = 15000;" in SOURCE
    assert 'line.startsWith("WDG1 START ")' in SOURCE
    assert "WDG1 LEASE EXPIRED" in SOURCE
    for setter in (
        "radio->setFrequency(frequency)",
        "radio->setBandwidth(bandwidth)",
        "radio->setSpreadingFactor(spreadingFactor)",
        "radio->setCodingRate(codingRate)",
    ):
        assert setter in SOURCE
    assert BRIDGE.MESHCORE_CONFIG_URL == "https://api.meshcore.nz/api/v1/config"
    assert "suggested_radio_settings" in BRIDGE_SOURCE


def test_bridge_keeps_key_out_of_cli_config_device_and_logs():
    assert 'add_argument("--api-key"' not in BRIDGE_SOURCE
    assert 'add_argument("--url"' not in BRIDGE_SOURCE
    assert "getpass(" in BRIDGE_SOURCE
    assert "keyring.set_password" in BRIDGE_SOURCE
    assert "keyring.get_password" in BRIDGE_SOURCE
    assert "api_key" not in json.dumps(
        {"version": 1, "device": {"serial_number": "abc"}, "region": {"title": "test"}}
    )
    assert 'write_line(device, "WDG1 AUTH OK")' in BRIDGE_SOURCE
    assert "write_line(device, api_key)" not in BRIDGE_SOURCE
    assert "api_key)" not in SETUP_SOURCE
    assert BRIDGE.KEYRING_SERVICE == "Canadaverse WDG Mesh Serial Bridge"
    assert "canadaverse-wdg-mesh-serial" in BRIDGE_SOURCE


def test_bridge_is_live_only_dedupes_in_memory_and_never_replays_a_post():
    assert "pending: dict[str, dict[str, Any]] = {}" in BRIDGE_SOURCE
    assert "attempted: set[str] = set()" in BRIDGE_SOURCE
    assert "attempted.update(pending)" in BRIDGE_SOURCE
    assert BRIDGE_SOURCE.index("attempted.update(pending)") < BRIDGE_SOURCE.index("result = upload(nodes, api_key)")
    assert "pending.clear()" in BRIDGE_SOURCE
    for token in ("sqlite", "history", "backfill", "replay.json", "observations.json"):
        assert token not in BRIDGE_SOURCE.lower()


def test_wdg_endpoint_and_waf_user_agent_are_fixed():
    assert BRIDGE.WDG_UPLOAD_URL == "https://wdgwars.pl/api/upload/"
    assert BRIDGE.WDG_ME_URL == "https://wdgwars.pl/api/me"
    assert BRIDGE.user_agent().startswith("WatchDogsGo/2.0 ")
    assert 'request.add_header("X-API-Key", api_key)' in BRIDGE_SOURCE


def test_all_five_exact_serial_targets_are_pinned_in_ci():
    targets = (
        "rcc6_wdg_serial",
        "heltec_v3_wdg_serial",
        "heltec_v4_wdg_serial",
        "rak4631_wdg_serial",
        "heltec_tracker_wdg_serial",
    )
    assert "default_envs = " + ", ".join(targets) in PLATFORMIO
    for target in targets:
        assert f"[env:{target}]" in PLATFORMIO
        assert f"environment: {target}" in WORKFLOW
    assert "platformio/espressif32@6.11.0" in PLATFORMIO
    assert "MeshCore.git#727fc0512ce08bfd7b499e46daa7fca6eeec730d" in PLATFORMIO
    assert "jgromes/RadioLib @ 7.7.1" in PLATFORMIO
    assert "-D WDG_SERIAL_ONLY=1" in PLATFORMIO
    assert "post:scripts/package_firmware.py" in PLATFORMIO
    assert "firmware-factory.bin" in WORKFLOW
    assert "merge_bin" in PACKAGE_SCRIPT
    assert '"$BUILD_DIR/${PROGNAME}.bin"' in PACKAGE_SCRIPT
    assert "str(source[0])" not in PACKAGE_SCRIPT
    assert "str(target[0])" not in PACKAGE_SCRIPT


def test_rak4631_uses_upstream_wisblock_radio_power_and_pin_map():
    for token in (
        "constexpr int kLoRaSclk = 43;",
        "constexpr int kLoRaMiso = 45;",
        "constexpr int kLoRaMosi = 44;",
        "constexpr int kLoRaNss = 42;",
        "constexpr int kLoRaDio1 = 47;",
        "constexpr int kLoRaBusy = 46;",
        "constexpr int kLoRaReset = 38;",
    ):
        assert token in BOARD_HEADER
    assert "constexpr int kSx126xPowerEnable = 37;" in BOARD_SOURCE
    assert "SPI.setPins(wdg_board::kLoRaMiso" in SOURCE
    assert "framework-arduinoadafruitnrf52" in PLATFORMIO
    assert (ROOT / "boards" / "rak4631.json").is_file()
    assert (ROOT / "variants" / "rak4631" / "variant.cpp").is_file()


def test_heltec_tracker_uses_upstream_integrated_sx1262_pins_headless():
    tracker = BOARD_HEADER[BOARD_HEADER.index("#elif defined(WDG_BOARD_HELTEC_TRACKER)") :]
    for token in (
        "constexpr int kLoRaSclk = 9;",
        "constexpr int kLoRaMiso = 11;",
        "constexpr int kLoRaMosi = 10;",
        "constexpr int kLoRaNss = 8;",
        "constexpr int kLoRaDio1 = 14;",
        "constexpr int kLoRaBusy = 13;",
        "constexpr int kLoRaReset = -1;",
    ):
        assert token in tracker
    assert "-D WDG_BOARD_HELTEC_TRACKER=1" in PLATFORMIO


def test_heltec_v3_reset_and_v4_front_end_contracts_are_preserved():
    v3_start = BOARD_HEADER.index("#elif defined(WDG_BOARD_HELTEC_V3)")
    v4_start = BOARD_HEADER.index("#elif defined(WDG_BOARD_HELTEC_V4)", v3_start)
    assert "constexpr int kLoRaReset = 12;" in BOARD_HEADER[v3_start:v4_start]
    for token in (
        "constexpr int kFemPowerPin = 7;",
        "constexpr int kFemSharedEnablePin = 2;",
        "constexpr int kFemGc1109TxPin = 46;",
        "constexpr int kFemKct8103TxPin = 5;",
        "radio.readRegister(0x08B5",
        "radio.writeRegister(0x08B5",
    ):
        assert token in BOARD_SOURCE
    assert "-I variants/heltec_v4" in PLATFORMIO
    assert "-D RADIOLIB_LOW_LEVEL=1" in PLATFORMIO


def test_minimal_meshcore_build_and_screenshot_diagnostics_are_preserved():
    for source in (
        "AdvertDataHelpers.cpp",
        "BaseChatMesh.cpp",
        "StaticPoolPacketManager.cpp",
        "TxtDataHelpers.cpp",
    ):
        assert source in BUILD_SCRIPT
    assert "volatile bool wdg_screenshot_requested = false;" in SOURCE
    assert 'line == "CMD:screenshot:"' in SOURCE
    assert "if (!wdg_screenshot_requested || framebuffer == nullptr) return;" in NV_DISPLAY
    assert "if (!wdg_screenshot_requested) return;" in BUILD_SCRIPT
    assert "binascii.crc32(payload)" in CAPTURE_TOOL


def test_rcc6_status_page_uses_its_220x128_build_override():
    render = SOURCE.index("void renderDisplay()")
    start = SOURCE.index("#if defined(WDG_BOARD_RCC6)", render)
    end = SOURCE.index("#elif defined(WDG_BOARD_HELTEC_V3)", start)
    rcc6_ui = SOURCE[start:end]
    assert "-D NV3001B_LOGICAL_WIDTH=220" in PLATFORMIO
    assert "-D NV3001B_LOGICAL_HEIGHT=128" in PLATFORMIO
    assert "display.fillRect(0, 0, 220, 20);" in rcc6_ui
    assert "display.drawTextCentered(110, 29" in rcc6_ui
    assert "display.drawTextCentered(110, 113, line);" in rcc6_ui


def test_setup_installs_into_a_local_virtual_environment_without_a_secret_argument():
    assert 'VENV = ROOT / ".venv"' in SETUP_SOURCE
    assert "venv.EnvBuilder(with_pip=True)" in SETUP_SOURCE
    assert '"configure"' in SETUP_SOURCE
    assert "api_key" not in SETUP_SOURCE.lower()
    assert "*sys.argv[1:]" in SETUP_SOURCE
    assert 'add_argument(\n        "--port"' in BRIDGE_SOURCE
    assert 'add_argument(\n        "--region"' in BRIDGE_SOURCE


def test_windows_background_tray_controls_only_the_safe_bridge_task():
    for token in (
        "CanadaverseWDGMeshBridge",
        "CanadaverseWDGMeshTray",
        "Register-ScheduledTask",
        "New-ScheduledTaskTrigger -AtLogOn",
        "Start-ScheduledTask",
        "Stop-ScheduledTask",
        ".venv\\Scripts\\pythonw.exe",
        "windows_tray_host.vbs",
        "System32\\wscript.exe",
        "Windows.Forms.NotifyIcon",
        "Windows.Forms.ContextMenuStrip",
    ):
        assert token in WINDOWS_BACKGROUND
    assert "WDG_API_KEY" not in WINDOWS_BACKGROUND
    assert "--api-key" not in WINDOWS_BACKGROUND
    assert re.findall(r"(?i)(?<![0-9a-f])[0-9a-f]{64}(?![0-9a-f])", WINDOWS_BACKGROUND) == []
    assert 'CreateObject("WScript.Shell")' in WINDOWS_TRAY_HOST
    assert "shell.Run(command, 0, True)" in WINDOWS_TRAY_HOST
    assert "WDG_API_KEY" not in WINDOWS_TRAY_HOST
