#ifdef OBL_HANDHELD

// The handheld build's panel (docs/build/handheld.md). Anbernic's stock system reaches its panel only through its own SDL2, which has
// the Mali video driver; the game's static SDL3 has no video driver there. A thread in the game shows the frames through that SDL2 and
// handles the console's volume keys. SDL3 and SDL2 share their function names, so SDL2 is loaded with dlopen and called only through
// the pointers in Sdl2: RTLD_DEEPBIND makes SDL2 look up its own symbols before the game's, which -rdynamic exports. This file sees
// SDL2's headers (OBL_SDL2_INCLUDE_DIR) and never SDL3's.

#include "platform/frame_share.h"

#include "core/log.h"
#include "platform/frame_bands.h"

#include <SDL.h>
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace obl::platform {

namespace {

using Clock = std::chrono::steady_clock;

// ---- the hand-over ----------------------------------------------------------------------------------------------------------------
// Allocated once and never freed: the panel thread may still run while the process exits without SharedFrameClose (a fatal error).
struct Shared {
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<uint8_t> pixels; // the latest frame: height rows of width * 2 bytes
    int width = 0, height = 0;
    bool rgb555 = false;
    SharedCursor cursor;
    uint64_t seq = 0; // counts handed-over frames
    bool stop = false;
    std::thread* thread = nullptr; // started and joined by the game's thread
    std::atomic<bool> failed{false};
};

Shared& State()
{
    static Shared* shared = new Shared;
    return *shared;
}

// ---- the firmware's SDL2 ----------------------------------------------------------------------------------------------------------
struct Sdl2 {
    decltype(&SDL_SetHint) SetHint;
    decltype(&SDL_Init) Init;
    decltype(&SDL_Quit) Quit;
    decltype(&SDL_GetError) GetError;
    decltype(&SDL_GetCurrentVideoDriver) GetCurrentVideoDriver;
    decltype(&SDL_CreateWindow) CreateWindow;
    decltype(&SDL_DestroyWindow) DestroyWindow;
    decltype(&SDL_CreateRenderer) CreateRenderer;
    decltype(&SDL_DestroyRenderer) DestroyRenderer;
    decltype(&SDL_GetRendererOutputSize) GetRendererOutputSize;
    decltype(&SDL_CreateTexture) CreateTexture;
    decltype(&SDL_DestroyTexture) DestroyTexture;
    decltype(&SDL_UpdateTexture) UpdateTexture;
    decltype(&SDL_SetTextureBlendMode) SetTextureBlendMode;
    decltype(&SDL_SetRenderDrawColor) SetRenderDrawColor;
    decltype(&SDL_SetRenderDrawBlendMode) SetRenderDrawBlendMode;
    decltype(&SDL_RenderClear) RenderClear;
    decltype(&SDL_RenderCopy) RenderCopy;
    decltype(&SDL_RenderFillRect) RenderFillRect;
    decltype(&SDL_RenderDrawRect) RenderDrawRect;
    decltype(&SDL_RenderPresent) RenderPresent;
    decltype(&SDL_ShowCursor) ShowCursor;
    decltype(&SDL_PollEvent) PollEvent;
};

template<class Function> bool Resolve(void* lib, Function& function, const char* name)
{
    function = reinterpret_cast<Function>(dlsym(lib, name));
    if (!function) OBL_LOG("panel: libSDL2 has no %s", name);
    return function != nullptr;
}

// The library the console's menu finds through its LD_LIBRARY_PATH (/usr/lib: Anbernic's SDL2 with the Mali driver). It stays loaded:
// unloading a GPU driver while the process runs on is not worth the risk.
bool LoadSdl2(Sdl2& s)
{
    void* lib = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
    if (!lib) {
        OBL_LOG("panel: %s", dlerror());
        return false;
    }
    return Resolve(lib, s.SetHint, "SDL_SetHint") && Resolve(lib, s.Init, "SDL_Init") && Resolve(lib, s.Quit, "SDL_Quit") &&
            Resolve(lib, s.GetError, "SDL_GetError") && Resolve(lib, s.GetCurrentVideoDriver, "SDL_GetCurrentVideoDriver") &&
            Resolve(lib, s.CreateWindow, "SDL_CreateWindow") && Resolve(lib, s.DestroyWindow, "SDL_DestroyWindow") &&
            Resolve(lib, s.CreateRenderer, "SDL_CreateRenderer") && Resolve(lib, s.DestroyRenderer, "SDL_DestroyRenderer") &&
            Resolve(lib, s.GetRendererOutputSize, "SDL_GetRendererOutputSize") && Resolve(lib, s.CreateTexture, "SDL_CreateTexture") &&
            Resolve(lib, s.DestroyTexture, "SDL_DestroyTexture") && Resolve(lib, s.UpdateTexture, "SDL_UpdateTexture") &&
            Resolve(lib, s.SetTextureBlendMode, "SDL_SetTextureBlendMode") &&
            Resolve(lib, s.SetRenderDrawColor, "SDL_SetRenderDrawColor") &&
            Resolve(lib, s.SetRenderDrawBlendMode, "SDL_SetRenderDrawBlendMode") && Resolve(lib, s.RenderClear, "SDL_RenderClear") &&
            Resolve(lib, s.RenderCopy, "SDL_RenderCopy") && Resolve(lib, s.RenderFillRect, "SDL_RenderFillRect") &&
            Resolve(lib, s.RenderDrawRect, "SDL_RenderDrawRect") && Resolve(lib, s.RenderPresent, "SDL_RenderPresent") &&
            Resolve(lib, s.ShowCursor, "SDL_ShowCursor") && Resolve(lib, s.PollEvent, "SDL_PollEvent");
}

// ---- volume ------------------------------------------------------------------------------------------------------------------------
// The firmware's volume keys have no handler while a port runs: the panel moves ALSA's level and shows it for a moment.
constexpr const char* kMixerCard = "hw:0";
constexpr const char* kVolumeControl = "lineout volume";
constexpr int kVolumeSteps = 16;
constexpr int kVolumeDefault = kVolumeSteps / 2; // the first start: the console at full volume is far too loud for the game
constexpr const char* kVolumeFile = "obl_volume.txt"; // in the game directory: the level the player chose, 0..kVolumeSteps
constexpr auto kOverlayTime = std::chrono::milliseconds(1500);

class Volume {
public:
    // Sets the saved level (kVolumeDefault the first time) and remembers the console's own level for Restore.
    void Open()
    {
        snd_mixer_selem_id_t* id;
        if (snd_mixer_open(&m_mixer, 0) < 0 || snd_mixer_attach(m_mixer, kMixerCard) < 0 ||
                snd_mixer_selem_register(m_mixer, nullptr, nullptr) < 0 || snd_mixer_load(m_mixer) < 0) {
            OBL_LOG("panel: mixer unavailable");
            m_mixer = nullptr;
            return;
        }
        snd_mixer_selem_id_alloca(&id);
        snd_mixer_selem_id_set_name(id, kVolumeControl);
        m_volume = snd_mixer_find_selem(m_mixer, id);
        if (!m_volume) {
            OBL_LOG("panel: no mixer control '%s'", kVolumeControl);
            return;
        }
        snd_mixer_selem_get_playback_volume_range(m_volume, &m_min, &m_max);
        snd_mixer_selem_get_playback_volume(m_volume, SND_MIXER_SCHN_MONO, &m_atStart);
        int step = kVolumeDefault;
        if (std::FILE* file = std::fopen(kVolumeFile, "r")) {
            if (std::fscanf(file, "%d", &step) != 1 || step < 0 || step > kVolumeSteps) step = kVolumeDefault;
            std::fclose(file);
        }
        Set(step);
    }

    // One step up or down, from where the mixer is now (another program may have moved it).
    int Change(int delta)
    {
        int step = kVolumeSteps;
        if (m_volume && m_max > m_min) {
            long value = 0;
            snd_mixer_handle_events(m_mixer);
            snd_mixer_selem_get_playback_volume(m_volume, SND_MIXER_SCHN_MONO, &value);
            step = static_cast<int>(std::lround(static_cast<double>(value - m_min) * kVolumeSteps / static_cast<double>(m_max - m_min)));
        }
        step = std::clamp(step + delta, 0, kVolumeSteps);
        Set(step);
        if (std::FILE* file = std::fopen(kVolumeFile, "w")) {
            std::fprintf(file, "%d\n", step);
            std::fclose(file);
        }
        return step;
    }

    // The console's own level back, for the menu and the other programs.
    void Restore()
    {
        if (m_volume) snd_mixer_selem_set_playback_volume_all(m_volume, m_atStart);
        if (m_mixer) snd_mixer_close(m_mixer);
        m_mixer = nullptr;
        m_volume = nullptr;
    }

    bool Available() const { return m_volume != nullptr; }

private:
    void Set(int step)
    {
        if (!m_volume) return;
        snd_mixer_selem_set_playback_volume_all(
                m_volume, m_min + static_cast<long>(std::lround(static_cast<double>(step) * (m_max - m_min) / kVolumeSteps)));
    }

    snd_mixer_t* m_mixer = nullptr;
    snd_mixer_elem_t* m_volume = nullptr;
    long m_min = 0, m_max = 0, m_atStart = 0;
};

// The console's key device: the one that reports the volume keys.
int OpenKeyDevice()
{
    for (int i = 0; i < 16; ++i) {
        char path[32], name[128] = "";
        std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
        const int fd = ::open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        if (::ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && std::strstr(name, "ANBERNIC-keys")) return fd;
        ::close(fd);
    }
    return -1;
}

// ---- drawing -----------------------------------------------------------------------------------------------------------------------
// X outline, . fill: the cursor, 12 x 17.
constexpr const char* kArrow[17] = {
        "X           ",
        "XX          ",
        "X.X         ",
        "X..X        ",
        "X...X       ",
        "X....X      ",
        "X.....X     ",
        "X......X    ",
        "X.......X   ",
        "X........X  ",
        "X.....XXXXX ",
        "X..X..X     ",
        "X.X X..X    ",
        "XX  X..X    ",
        "X    X..X   ",
        "     X..X   ",
        "      XX    ",
};

// 3 x 5 glyphs of "0123456789%", one row per 3 bits.
constexpr unsigned char kGlyphs[11][5] = {
        {7, 5, 5, 5, 7},
        {2, 6, 2, 2, 7},
        {7, 1, 7, 4, 7},
        {7, 1, 7, 1, 7},
        {5, 5, 7, 1, 1},
        {7, 4, 7, 1, 7},
        {7, 4, 7, 5, 7},
        {7, 1, 1, 1, 1},
        {7, 5, 7, 5, 7},
        {7, 5, 7, 1, 7},
        {5, 1, 2, 4, 5},
};

SDL_Texture* MakeCursor(const Sdl2& s, SDL_Renderer* renderer)
{
    Uint32 pixels[17 * 12];
    for (int y = 0; y < 17; ++y)
        for (int x = 0; x < 12; ++x) {
            const char c = kArrow[y][x];
            pixels[y * 12 + x] = c == 'X' ? 0xff000000u : c == '.' ? 0xffffffffu : 0u;
        }
    SDL_Texture* texture = s.CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 12, 17);
    if (!texture) return nullptr;
    s.UpdateTexture(texture, nullptr, pixels, 12 * 4);
    s.SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

// A bar with the level in percent, at the bottom of the panel.
void DrawVolume(const Sdl2& s, SDL_Renderer* renderer, int outW, int outH, int step)
{
    const int panelW = 220, panelH = 36;
    const SDL_Rect panel = {(outW - panelW) / 2, outH - panelH - 24, panelW, panelH};
    s.SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    s.SetRenderDrawColor(renderer, 0, 0, 0, 200);
    s.RenderFillRect(renderer, &panel);
    s.SetRenderDrawColor(renderer, 255, 255, 255, 255);
    s.RenderDrawRect(renderer, &panel);
    const SDL_Rect track = {panel.x + 12, panel.y + 12, 140, 12};
    s.RenderDrawRect(renderer, &track);
    const SDL_Rect fill = {track.x + 2, track.y + 2, (track.w - 4) * step / kVolumeSteps, track.h - 4};
    s.RenderFillRect(renderer, &fill);
    char text[16];
    std::snprintf(text, sizeof(text), "%d%%", step * 100 / kVolumeSteps);
    int x = panel.x + 164;
    for (const char* c = text; *c; ++c, x += 12) {
        const int glyph = *c == '%' ? 10 : *c - '0';
        for (int row = 0; row < 5; ++row)
            for (int column = 0; column < 3; ++column)
                if (kGlyphs[glyph][row] & (4 >> column)) {
                    const SDL_Rect dot = {x + column * 3, panel.y + 10 + row * 3, 3, 3};
                    s.RenderFillRect(renderer, &dot);
                }
    }
    s.SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

// ---- the panel thread --------------------------------------------------------------------------------------------------------------
void PanelMain()
{
    pthread_setname_np(pthread_self(), "obl-panel"); // its own line in top -H and /proc/<pid>/task
    Shared& shared = State();
    Sdl2 s{};
    if (!LoadSdl2(s)) {
        shared.failed = true;
        return;
    }
    s.SetHint("SDL_NO_SIGNAL_HANDLERS", "1"); // SIGINT and SIGTERM stay the game's (SDL3 ends the game on them)
    // Linear: the menus and screens before the game are drawn for 800x600 and shown at 0.8 on the 640x480 panel, where nearest
    // sampling drops pixel columns and breaks small text. The game at 640x480 is shown 1:1, which linear leaves exact.
    s.SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (s.Init(SDL_INIT_VIDEO) != 0) {
        OBL_LOG("panel: SDL2 video: %s", s.GetError());
        shared.failed = true;
        return;
    }
    SDL_Window* window = s.CreateWindow("OpenBeach", 0, 0, 640, 480, SDL_WINDOW_FULLSCREEN);
    SDL_Renderer* renderer = window ? s.CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!renderer) {
        OBL_LOG("panel: SDL2 window: %s", s.GetError());
        if (window) s.DestroyWindow(window);
        s.Quit();
        shared.failed = true;
        return;
    }
    s.ShowCursor(SDL_DISABLE);
    SDL_Texture* cursorTexture = MakeCursor(s, renderer);
    Volume volume;
    volume.Open();
    const int keys = OpenKeyDevice();
    OBL_LOG("panel: SDL2 video driver %s, mixer %s, keys %s", s.GetCurrentVideoDriver(), volume.Available() ? "ok" : "none",
            keys >= 0 ? "ok" : "none");

    SDL_Texture* texture = nullptr;
    int texW = 0, texH = 0;
    bool texRgb555 = false;
    // Frames are never copied here: the game's buffer is swapped for `current` under the lock, and `current` then becomes `previous`.
    std::vector<uint8_t> current; // the frame being prepared
    std::vector<uint8_t> previous; // the frame as last shown, to find what changed
    std::vector<uint32_t> rgba; // the changed rows as the texture takes them
    std::vector<RowRun> runs;
    SharedCursor cursor;
    uint64_t lastSeq = 0;
    int volumeStep = 0;
    Clock::time_point overlayUntil{};
    bool overlay = false, dirty = true;

    std::unique_lock lock(shared.mutex);
    for (;;) {
        // A new frame, the end, or 20 ms for the volume keys and the bar.
        shared.wake.wait_for(lock, std::chrono::milliseconds(20), [&] { return shared.stop || shared.seq != lastSeq; });
        if (shared.stop) break;
        bool fresh = false, newTexture = false;
        int w = texW, h = texH;
        bool rgb555 = texRgb555;
        SharedCursor next = cursor;
        if (shared.seq != lastSeq && shared.width > 0) {
            // The lock covers only the swap: the game gets back a buffer it overwrites in full, and never waits for the work below.
            lastSeq = shared.seq;
            current.swap(shared.pixels);
            w = shared.width;
            h = shared.height;
            rgb555 = shared.rgb555;
            next = shared.cursor;
            fresh = true;
        }
        lock.unlock();

        if (fresh) {
            const size_t pitch = static_cast<size_t>(w) * 2;
            newTexture = !texture || w != texW || h != texH || rgb555 != texRgb555;
            texW = w;
            texH = h;
            texRgb555 = rgb555;
            rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
            runs = ChangedRows(previous.data(), current.data(), pitch, h, newTexture || previous.size() != current.size());
            for (const RowRun& run : runs)
                for (int y = run.begin; y < run.end; ++y)
                    RowToRgba(reinterpret_cast<const uint16_t*>(current.data() + static_cast<size_t>(y) * pitch),
                            rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(w), w, texRgb555);
            previous.swap(current); // what the texture will show; `current` goes back to the game with the next swap
            if (!runs.empty() || next.visible != cursor.visible || (next.visible && (next.x != cursor.x || next.y != cursor.y)))
                dirty = true;
            cursor = next;
        }

        SDL_Event event;
        while (s.PollEvent(&event)) {}
        input_event key;
        while (keys >= 0 && ::read(keys, &key, sizeof(key)) == static_cast<ssize_t>(sizeof(key))) {
            // A press or a held repeat moves the level one step.
            if (key.type != EV_KEY || key.value == 0 || (key.code != KEY_VOLUMEUP && key.code != KEY_VOLUMEDOWN)) continue;
            volumeStep = volume.Change(key.code == KEY_VOLUMEUP ? 1 : -1);
            overlayUntil = Clock::now() + kOverlayTime;
            overlay = dirty = true;
        }
        if (overlay && Clock::now() >= overlayUntil) {
            overlay = false;
            dirty = true;
        }

        if (newTexture) {
            if (texture) s.DestroyTexture(texture);
            texture = s.CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, texW, texH);
            if (!texture) OBL_LOG("panel: SDL2 texture %dx%d: %s", texW, texH, s.GetError());
            if (texture) s.SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
        }
        for (const RowRun& run : runs) {
            if (!texture) break;
            const SDL_Rect rect = {0, run.begin, texW, run.end - run.begin};
            s.UpdateTexture(texture, &rect, rgba.data() + static_cast<size_t>(run.begin) * static_cast<size_t>(texW), texW * 4);
        }
        runs.clear();

        if (dirty) {
            dirty = false;
            int outW = 0, outH = 0;
            s.GetRendererOutputSize(renderer, &outW, &outH);
            s.SetRenderDrawColor(renderer, 0, 0, 0, 255);
            s.RenderClear(renderer);
            if (texture) {
                const double scale = std::min(static_cast<double>(outW) / texW, static_cast<double>(outH) / texH);
                SDL_Rect picture;
                picture.w = static_cast<int>(std::lround(texW * scale));
                picture.h = static_cast<int>(std::lround(texH * scale));
                picture.x = (outW - picture.w) / 2;
                picture.y = (outH - picture.h) / 2;
                s.RenderCopy(renderer, texture, nullptr, &picture);
                if (cursorTexture && cursor.visible) {
                    const SDL_Rect at = {
                            picture.x + static_cast<int>(cursor.x * scale), picture.y + static_cast<int>(cursor.y * scale), 12, 17};
                    s.RenderCopy(renderer, cursorTexture, nullptr, &at);
                }
            }
            if (overlay) DrawVolume(s, renderer, outW, outH, volumeStep);
            s.RenderPresent(renderer);
        }
        lock.lock();
    }
    lock.unlock();

    if (texture) s.DestroyTexture(texture);
    if (cursorTexture) s.DestroyTexture(cursorTexture);
    s.DestroyRenderer(renderer);
    s.DestroyWindow(window);
    s.Quit();
    if (keys >= 0) ::close(keys);
    volume.Restore();
    OBL_LOG("panel: closed");
}

} // namespace

bool SharedFramePresent(const uint16_t* bits, int pitch, int width, int height, bool rgb555, const SharedCursor& cursor)
{
    Shared& shared = State();
    if (shared.failed || !bits || width <= 0 || height <= 0) return false;
    {
        std::lock_guard lock(shared.mutex);
        const size_t rowBytes = static_cast<size_t>(width) * 2;
        shared.pixels.resize(rowBytes * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
            std::memcpy(shared.pixels.data() + static_cast<size_t>(y) * rowBytes,
                    reinterpret_cast<const uint8_t*>(bits) + static_cast<size_t>(y) * static_cast<size_t>(pitch), rowBytes);
        shared.width = width;
        shared.height = height;
        shared.rgb555 = rgb555;
        shared.cursor = cursor;
        ++shared.seq;
    }
    if (!shared.thread) shared.thread = new std::thread(PanelMain);
    shared.wake.notify_one();
    return true;
}

void SharedFrameClose()
{
    Shared& shared = State();
    if (!shared.thread) return;
    {
        std::lock_guard lock(shared.mutex);
        shared.stop = true;
    }
    shared.wake.notify_one();
    shared.thread->join();
    delete shared.thread;
    shared.thread = nullptr;
    std::lock_guard lock(shared.mutex);
    shared.stop = false; // a later frame starts the panel again
}

} // namespace obl::platform

#endif // OBL_HANDHELD
