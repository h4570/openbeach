#include "old_saves.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    // A converter that writes something and then stalls, for the timeout/partial cleanup check.
    if (argc == 5 && std::string(argv[1]) == "save") {
        std::ofstream(std::filesystem::path(argv[4])) << "unfinished";
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    if (argc != 4 && argc != 5) return 2;
    auto result = obl::launcher::ConvertOldSaves(
            argv[1], argv[2], argv[3], [](const std::string& line) { printf("%s\n", line.c_str()); },
            std::chrono::milliseconds(argc == 5 ? std::stoll(argv[4]) : 300000));
    for (const auto& name : result.converted) printf("converted=%s\n", name.c_str());
    for (const auto& name : result.failed) printf("failed=%s\n", name.c_str());
    return result.failed.empty() ? 0 : 1;
}
