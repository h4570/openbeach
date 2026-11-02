// Writes a synthetic old save (tests/unit/old_save_builder.h) to the given path, so the Python save tests
// (test_convert_old_saves.py, test_launcher_startup_old_saves.py) run without the user's saves.

#include "old_save_builder.h"

#include <cstdio>
#include <fstream>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: synthetic_old_save <out.dat>\n");
        return 2;
    }
    const obl::test::Save save = obl::test::Save::Make(1, {}, 4); // a 2 x 2 grid: the converter needs a square one
    std::ofstream out(argv[1], std::ios::binary);
    out.write(reinterpret_cast<const char*>(save.data.data()), static_cast<std::streamsize>(save.data.size()));
    return out ? 0 : 1;
}
