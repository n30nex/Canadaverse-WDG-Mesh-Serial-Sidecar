#!/usr/bin/env python3
"""Capture one CRC-verified framebuffer from a running WDG Mesh Sidecar."""

from __future__ import annotations

import argparse
import binascii
import pathlib
import struct
import time


MAGIC = b"NPOF"
HEADER = struct.Struct("<IHHHI")


def read_exact(port, length: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < length:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"received {len(data)}/{length} bytes")
        chunk = port.read(length - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def find_magic(port, deadline: float) -> None:
    window = bytearray()
    while bytes(window) != MAGIC:
        if time.monotonic() >= deadline:
            raise TimeoutError("screenshot header not received")
        byte = port.read(1)
        if byte:
            window.extend(byte)
            del window[:-len(MAGIC)]


def read_frame(port, timeout: float) -> tuple[int, int, int, bytes]:
    deadline = time.monotonic() + timeout
    find_magic(port, deadline)
    sequence, width, height, length, expected_crc = HEADER.unpack(
        read_exact(port, HEADER.size, deadline)
    )
    payload = read_exact(port, length, deadline)
    actual_crc = binascii.crc32(payload) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        raise RuntimeError(
            f"CRC mismatch: expected {expected_crc:08x}, received {actual_crc:08x}"
        )
    if (width, height, length) not in ((128, 64, 1024), (220, 128, 56320)):
        raise RuntimeError(f"unexpected frame geometry: {width}x{height}, {length} bytes")
    return sequence, width, height, payload


def decode_frame(width: int, height: int, payload: bytes):
    try:
        from PIL import Image
    except ImportError as error:
        raise SystemExit("Pillow is required: python -m pip install Pillow") from error

    if len(payload) == width * height // 8:
        image = Image.new("RGB", (width, height), "black")
        pixels = image.load()
        for y in range(height):
            for x in range(width):
                if payload[x + (y // 8) * width] & (1 << (y & 7)):
                    pixels[x, y] = (255, 255, 255)
        return image
    if len(payload) != width * height * 2:
        raise ValueError("unsupported framebuffer length")
    rgb = bytearray(width * height * 3)
    for index, (value,) in enumerate(struct.iter_unpack("<H", payload)):
        offset = index * 3
        rgb[offset] = ((value >> 11) & 0x1F) * 255 // 31
        rgb[offset + 1] = ((value >> 5) & 0x3F) * 255 // 63
        rgb[offset + 2] = (value & 0x1F) * 255 // 31
    return Image.frombytes("RGB", (width, height), bytes(rgb))


def self_test() -> None:
    class Fragmented:
        def __init__(self, data: bytes):
            self.data = bytearray(data)

        def read(self, length: int) -> bytes:
            length = min(length, 3, len(self.data))
            chunk = self.data[:length]
            del self.data[:length]
            return bytes(chunk)

    oled = bytes([1]) + bytes(1023)
    crc = binascii.crc32(oled) & 0xFFFFFFFF
    stream = Fragmented(b"boot log\n" + MAGIC + HEADER.pack(7, 128, 64, 1024, crc) + oled)
    assert read_frame(stream, 1) == (7, 128, 64, oled)
    assert decode_frame(128, 64, oled).getpixel((0, 0)) == (255, 255, 255)
    print("self-test passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="verified device serial port")
    parser.add_argument("--output", type=pathlib.Path, help="PNG output path")
    parser.add_argument("--scale", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.port or args.output is None:
        parser.error("--port and --output are required")
    if args.scale < 1 or args.timeout <= 0:
        parser.error("--scale and --timeout must be positive")

    try:
        import serial
    except ImportError as error:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from error

    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = 0.1
    port.write_timeout = 3
    port.dtr = False
    port.rts = False
    port.open()
    try:
        # Opening native USB can restart an ESP32-S3. Let networking and display
        # setup finish so the diagnostic command cannot capture an early boot frame.
        time.sleep(10.0)
        port.reset_input_buffer()
        port.write(b"CMD:screenshot:\n")
        port.flush()
        sequence, width, height, payload = read_frame(port, args.timeout)
    finally:
        port.close()

    image = decode_frame(width, height, payload)
    if args.scale != 1:
        from PIL import Image

        image = image.resize(
            (width * args.scale, height * args.scale), Image.Resampling.NEAREST
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    print(f"captured frame {sequence}: {width}x{height} -> {args.output}")


if __name__ == "__main__":
    main()
