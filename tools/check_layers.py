"""Lint the layers src/core, src/formats, src/game, src/platform and the shells dll, launcher, host_linux."""
import argparse
import re
import sys
from pathlib import Path

from include_graph import load_graph

ROOT = Path(__file__).resolve().parent.parent
# Allowed include direction: shells -> testing -> platform -> game -> core, never upward. The test interpreter
# src/testing/ ranks above platform (its Windows-only tools are platform/test_tools/), the DLL shell and the hosts above
# it; the offline tools (convert, imgdiff) and the mss32 stub are not runtime code and stay unranked
# (docs/testing/game-tests.md). src/formats/ holds the own pack/save contracts the game and the importer share;
# no runtime layer includes the importer, only the test interpreter's conversion commands may
# (docs/architecture/data-formats/converter/index.md).
IMPORTER = "importer/"
IMPORTER_USERS = ("src/testing/",)
# The importer stays a separate program: it reaches portable code and the audited, header-only game tables, and links
# nothing of the game's implementation (importer-boundary.md "Outside-converter audit").
IMPORTER_REACH = ("importer/", "src/core/", "src/formats/")
IMPORTER_GAME_HEADERS = {
    "src/game/objects/guest_sets.h", "src/game/tables/building_behavior.h", "src/game/tables/world_params_leaves.h",
    "src/game/tables/world_params_reset.h", "src/game/tables/world_params_tables.h", "src/game/ui/audio_types.h",
    "src/game/ui/scripted_sound.h", "src/game/world/world_params_queries.h"}
IMPORTER_BUILD_FILES = ("importer/CMakeLists.txt", "importer/portable_sources.cmake", "importer/web/CMakeLists.txt",
                        "src/formats/CMakeLists.txt")
GAME_BUILD_FILE = "src/game/CMakeLists.txt"
GAME_LINKAGE = re.compile(r'\bobl_common\b|src/game/\S*\.cpp|\bobl_game\w*|src/(?:platform|testing|dll)/')
CONVERTER_LINKAGE = re.compile(r'\bobl_convert\b|\bobl_import_web\b')
RANK = {"src/core/": 0, "src/formats/": 0.5, "src/game/": 1, "src/platform/": 2, "src/testing/": 3, "src/dll/": 4, "src/launcher/": 4,
        "src/host_linux/": 4}
WINDOWS_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"](?:windows|winsock2?|ws2tcpip|shellapi|shlobj|direct\w+|mmsystem|dsound)\.h[>"]',
                             re.MULTILINE | re.IGNORECASE)


def layer(name):
    for prefix, rank in RANK.items():
        if name.startswith(prefix):
            return prefix, rank
    return None, None


def forbidden_reach(name, edges):
    """First forbidden header reachable from name (directly or transitively), as an include chain."""
    seen, stack = {name}, [(name, [name])]
    while stack:
        node, chain = stack.pop()
        for dep in sorted(edges[node]):
            if dep in seen:
                continue
            seen.add(dep)
            if dep.startswith("src/common/layout/"):
                return chain + [dep]
            stack.append((dep, chain + [dep]))
    return None


def strip_cmake_comments(text):
    return re.sub(r'(?m)#.*$', '', text)


def check_build_files(root):
    """The importer and obl_formats link no game implementation, and the game library links no importer."""
    problems = []
    for name in IMPORTER_BUILD_FILES:
        path = root / name
        if path.exists():
            for match in GAME_LINKAGE.finditer(strip_cmake_comments(path.read_text(encoding="utf-8"))):
                problems.append(f"{name}: names game implementation ({match.group(0)})")
    path = root / GAME_BUILD_FILE
    if path.exists():
        for match in CONVERTER_LINKAGE.finditer(strip_cmake_comments(path.read_text(encoding="utf-8"))):
            problems.append(f"{GAME_BUILD_FILE}: links the importer ({match.group(0)})")
    return problems


def check(root):
    texts, edges = load_graph(root)
    problems = check_build_files(root)
    for name in sorted(texts):
        if name.startswith(IMPORTER):
            for dep in sorted(edges[name]):
                if not dep.startswith(IMPORTER_REACH) and dep not in IMPORTER_GAME_HEADERS:
                    problems.append(f"{name}: includes {dep} outside the importer's audited reach")
    for name, text in sorted(texts.items()):
        prefix, rank = layer(name)
        if prefix is None:
            continue
        if prefix in ("src/core/", "src/game/", "src/testing/") and WINDOWS_INCLUDE.search(text):
            problems.append(f"{name}: includes a Windows header")
        chain = forbidden_reach(name, edges)
        if chain:
            problems.append(f"{name}: reaches a layout/EXE boundary header ({' -> '.join(chain)})")
        for dep in sorted(edges[name]):
            if dep.startswith(IMPORTER) and not prefix.startswith(IMPORTER_USERS):
                problems.append(f"{name}: includes the importer ({dep})")
            _, dep_rank = layer(dep)
            if dep_rank is not None and dep_rank > rank:
                problems.append(f"{name}: includes {dep} (upward include)")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    problems = check(args.root.resolve())
    for problem in problems:
        print(problem)
    print(f"check_layers: {'FAIL, ' + str(len(problems)) + ' problem(s)' if problems else 'ok'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
