#!/usr/bin/env python3
"""Print platform sources excluded from the Linux simulation build."""
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
cmake = (root / "src/host_linux/CMakeLists.txt").read_text()
windows_only = re.search(r"set\(WINDOWS_ONLY\s+(.*?)\n\)", cmake, re.S)
if windows_only is None:
    raise SystemExit("WINDOWS_ONLY source list not found")
for path in re.findall(r"\$\{REPO\}/(src/\S+\.(?:cpp|c))", windows_only[1]):
    print(path)
