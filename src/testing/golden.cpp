#include "core/char16.h"
#include "golden.h"

#include "assertions.h"
#include "core/image_compare.h"
#include "core/log.h"
#include "core/os.h"
#include "testing/png.h"
#include "core/wide_path.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace obl::test {

namespace {

Text16 g_goldenDir;
Text16 g_captureDir;
bool g_update;

// One request at a time: the script blocks on `expect-shot` until the present callback has handled it.
enum class Request { None, Pending, Done };
Request g_request = Request::None;
std::string g_name;
int g_tolerance;
double g_maxPercent;
unsigned long long g_requestFrame;

// The frame after the request is normally presented at once; a state without presents must not hang the script.
constexpr unsigned long long kTimeoutFrames = 600;

std::string Utf8(const Text16& text)
{
    return WideUtf8(text);
}

Text16 Wide(const std::string& text)
{
    return Text16(text.begin(), text.end());
}

void SaveDiff(std::vector<uint32_t>& pixels, int width, int height)
{
    Text16 path = g_captureDir + Wide(g_name) + u"_diff.png";
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_XRGB8888, pixels.data(), width * 4);
    bool ok = surface && SDL_SavePNG(surface, PathUtf8(WidePath(path)).c_str());
    SDL_DestroySurface(surface);
    if (!ok) OBL_LOG("autotest: expect-shot %s cannot write the diff image: %s", g_name.c_str(), SDL_GetError());
}

void Compare(const uint16_t* bits, int width, int height, int pitchPixels)
{
    Text16 goldenPath = g_goldenDir + Wide(g_name) + u".png";
    if (g_update) {
        std::error_code error;
        std::filesystem::create_directory(WidePath(g_goldenDir), error);
        bool ok = WritePngRgb555(WidePath(goldenPath).c_str(), bits, width, height, pitchPixels);
        OBL_LOG("autotest: expect-shot %s recorded %dx%d %s", g_name.c_str(), width, height, ok ? "golden/" : "FAILED");
        return;
    }
    std::error_code error;
    if (!std::filesystem::exists(WidePath(goldenPath), error)) {
        OBL_LOG("autotest: SKIP expect-shot %s - no golden/%s.png; record it from this build: "
                "./tools/autotest.ps1 <script> -UpdateGolden",
                g_name.c_str(), g_name.c_str());
        return;
    }
    SDL_Surface* loaded = SDL_LoadPNG(PathUtf8(WidePath(goldenPath)).c_str());
    SDL_Surface* golden = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_XRGB8888) : nullptr;
    SDL_DestroySurface(loaded);
    if (!golden) {
        OBL_LOG("autotest: FAILED expect-shot %s cannot read golden/%s.png: %s", g_name.c_str(), g_name.c_str(), SDL_GetError());
        return;
    }
    image::ImageView actual{bits, width, height, pitchPixels * 2, image::PixelFormat::Rgb555};
    image::ImageView expected{golden->pixels, golden->w, golden->h, golden->pitch, image::PixelFormat::Xrgb8888};
    std::vector<uint32_t> diff;
    image::CompareResult r = image::Compare(actual, expected, g_tolerance, &diff);
    int goldenWidth = golden->w, goldenHeight = golden->h;
    SDL_DestroySurface(golden);
    if (!r.sizeMatch) {
        OBL_LOG("autotest: FAILED expect-shot %s size %dx%d, golden %dx%d", g_name.c_str(), width, height, goldenWidth, goldenHeight);
        return;
    }
    bool pass = r.DifferingPercent() <= g_maxPercent;
    OBL_LOG("autotest: %s expect-shot %s max_err=%d mean_err=%.4f diff_pixels=%llu diff_percent=%.4f tol=%d max_percent=%g",
            pass ? "PASS" : "FAILED", g_name.c_str(), r.maxChannelError, r.meanChannelError,
            static_cast<unsigned long long>(r.differingPixels), r.DifferingPercent(), g_tolerance, g_maxPercent);
    if (!pass) SaveDiff(diff, width, height);
}

} // namespace

void InitGolden(const Text16& gameDirectory, const Text16& captureDirectory)
{
    g_goldenDir = gameDirectory + u"golden\\";
    g_captureDir = captureDirectory;
    g_update = os::GetEnv("OBL_UPDATE_GOLDEN").value_or("").starts_with('1');
    if (g_update) OBL_LOG("autotest: golden update mode - expect-shot records golden/<name>.png");
}

bool StepExpectShot(const std::vector<std::string>& args)
{
    if (g_request == Request::Done) {
        g_request = Request::None;
        return true;
    }
    if (g_request == Request::Pending) {
        if (Frames() - g_requestFrame <= kTimeoutFrames) return false;
        OBL_LOG("autotest: FAILED expect-shot %s - no frame presented within %llu frames", g_name.c_str(), kTimeoutFrames);
        g_request = Request::None;
        return true;
    }
    if (args.empty()) {
        OBL_LOG("autotest: FAILED expect-shot needs a name");
        return true;
    }
    g_name = args[0];
    g_tolerance = args.size() > 1 ? std::atoi(args[1].c_str()) : 0;
    g_maxPercent = args.size() > 2 ? std::atof(args[2].c_str()) : 0.0;
    g_requestFrame = Frames();
    g_request = Request::Pending;
    return false;
}

bool GoldenShotPending()
{
    return g_request == Request::Pending;
}

void OnPresentGolden(const uint16_t* bits, int width, int height, int pitchPixels)
{
    if (g_request != Request::Pending) return;
    g_request = Request::Done;
    if (!bits) {
        OBL_LOG("autotest: FAILED expect-shot %s - no back buffer", g_name.c_str());
        return;
    }
    // The capture sits next to the other shots, so a failed comparison can be inspected (and diffed with obl_imgdiff).
    Text16 capture = g_captureDir + Wide(g_name) + u".png";
    WritePngRgb555(WidePath(capture).c_str(), bits, width, height, pitchPixels);
    Compare(bits, width, height, pitchPixels);
}

} // namespace obl::test
