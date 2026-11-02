"""The C++ include graph of src/ and importer/, shared by the layer lint and the progress report."""
import posixpath
import re

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)
SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".inl"}


def strip_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.DOTALL)


def load_graph(root):
    """Comment-free text of every C++ file under src/ and importer/ and its resolved include edges."""
    files = sorted(p for top in ("src", "importer") for p in (root / top).rglob("*") if p.suffix in SUFFIXES)
    names = {p.relative_to(root).as_posix(): p for p in files}
    roots = [root / "src/common", root / "src/app", root / "src/launcher", root / "src", root]
    texts, edges = {}, {}
    for name, path in names.items():
        # Ignore comments so a commented include cannot hold back a refactored file.
        texts[name] = strip_comments(path.read_text(encoding="utf-8"))
        edges[name] = set()
        for include in INCLUDE.findall(texts[name]):
            for base in [path.parent, *roots]:
                candidate = posixpath.normpath(f"{base.relative_to(root).as_posix()}/{include}")
                if candidate in names:
                    edges[name].add(candidate)
                    break
    return texts, edges
