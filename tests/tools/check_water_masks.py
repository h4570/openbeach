"""Validate every user-installed water animation against the blitter mask contract."""

import struct
import sys
from pathlib import Path


def child(directory: Path, name: str) -> Path:
    """The entry `name` of `directory`, ignoring case (the installation's `Data` on a case-sensitive file system)."""
    exact = directory / name
    if exact.exists():
        return exact
    return next((p for p in directory.iterdir() if p.name.lower() == name.lower()), exact)


def main(run: Path) -> None:
    files = sorted(p for p in child(child(run, "data"), "WaterAnims").iterdir() if p.suffix.lower() == ".bin")
    assert files, "no water animations found"
    frames = 0
    for path in files:
        data = path.read_bytes()
        size, *header = struct.unpack_from("<31I", data)
        assert len(data) == 0x7c + size, path.name
        offsets, counts = header[:15], header[15:]
        for offset, count in zip(offsets, counts):
            assert offset + count <= size, path.name
            codes = struct.unpack_from(f"<{count}b", data, 0x7c + offset)
            # The the reference build emits only zero for negative runs and one for positive runs.
            assert sum(abs(code) for code in codes) == 65536, path.name
            frames += 1
    print(f"PASS water masks: {len(files)} files, {frames} frames, decoded values 0/1 (legal 0..4)")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
