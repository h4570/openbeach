"""Recompress PNG IDAT losslessly; retain scanline bytes and all other chunks."""
import argparse
from pathlib import Path
import struct
import zlib


def compact(source: Path, destination: Path) -> None:
    data = source.read_bytes()
    signature = b"\x89PNG\r\n\x1a\n"
    if not data.startswith(signature):
        raise ValueError(f"not a PNG: {source}")
    chunks = []
    offset = len(signature)
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        end = offset + size + 12
        if end > len(data):
            raise ValueError("truncated PNG chunk")
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:end - 4]
        crc = struct.unpack_from(">I", data, end - 4)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError("invalid PNG CRC")
        chunks.append((kind, payload, data[offset:end]))
        offset = end
    raw = zlib.decompress(b"".join(p for k, p, _ in chunks if k == b"IDAT"))
    packed = zlib.compress(raw, 9)
    output = bytearray(signature)
    written = False
    for kind, _, original in chunks:
        if kind != b"IDAT":
            output.extend(original)
        elif not written:
            output.extend(struct.pack(">I", len(packed)) + b"IDAT" + packed)
            output.extend(struct.pack(">I", zlib.crc32(b"IDAT" + packed)))
            written = True
    destination.write_bytes(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    compact(args.source, args.destination)
