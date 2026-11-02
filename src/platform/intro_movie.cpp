// Intro movie (packs/base/video/intro.mpg, media_pack.h; MPEG-1 system stream: 640x480 video + MP2 audio).
//
// The movie runs before Renderer exists, so the player attaches the presenter's SDL renderer to the game window early
// (the game later presents through the same renderer). pl_mpeg decodes the stream and SDL shows it as a YUV texture,
// letterboxed like the game image; without any SDL renderer the movie is skipped. Skipping (Escape/Space/Enter, left
// button) comes from SDL events. The MP2 track plays through an SDL audio stream next to the game's sound and its
// playback position is the movie clock (wall clock without audio). The movie is a state of the host's main loop: each
// IntroMovieFrame call is one pass (game_host.h), so the browser build plays it without blocking.

#include "platform/intro_movie.h"

#include "core/config.h"
#include "game/game/game_accessors.h"
#include "platform/input.h"
#include "core/log.h"
#include "platform/window_placement.h"
#include "platform/sdl_present.h"
#include "game/ui/display_geometry.h"

#include <SDL3/SDL.h>
#include <cstdio> // pl_mpeg.h uses FILE without including it
#include <pl_mpeg.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace obl::platform {

namespace {

IntroMovieStats g_stats;
// The movie's sound after its last frame: the stream's queue (MovieAudio::kLeadSeconds) plays out well within it.
constexpr Uint64 kDrainLimitMs = 1000;

// Test switch: OBL_MOVIE_SKIP_MS=N pushes an Escape press as an SDL event after N ms of playback, so the skip takes the same
// event path as a physical key.
int SkipInjectionMs()
{
    const char* value = std::getenv("OBL_MOVIE_SKIP_MS");
    return value ? std::atoi(value) : -1;
}

void PushEscape()
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.timestamp = SDL_GetTicksNS();
    event.key.windowID = SdlGameWindowId();
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    event.key.key = SDLK_ESCAPE;
    event.key.down = true;
    SDL_PushEvent(&event);
}

bool IsSkipEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) return event.button.button == SDL_BUTTON_LEFT;
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return false;
    SDL_Scancode s = event.key.scancode;
    return s == SDL_SCANCODE_ESCAPE || s == SDL_SCANCODE_SPACE || s == SDL_SCANCODE_RETURN || s == SDL_SCANCODE_KP_ENTER;
}

// pl_mpeg decoder output: an SDL YUV texture on the presenter's renderer.
class MpegPlayer {
public:
    MpegPlayer(SDL_Window* window, SDL_Renderer* renderer, plm_t* plm) : m_window(window), m_renderer(renderer), m_plm(plm)
    {
        m_width = plm_get_width(plm);
        m_height = plm_get_height(plm);
        m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
        if (!m_texture) OBL_LOG("intro movie: SDL_CreateTexture failed: %s", SDL_GetError());
        plm_set_video_decode_callback(plm, &MpegPlayer::OnFrame, this);
    }

    ~MpegPlayer()
    {
        if (m_texture) SDL_DestroyTexture(m_texture);
    }

    MpegPlayer(const MpegPlayer&) = delete;
    MpegPlayer& operator=(const MpegPlayer&) = delete;

    bool Ready() const { return m_texture != nullptr; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    unsigned Frames() const { return m_frames; }

    // Decodes `seconds` of the stream; presents when a new frame arrived or the window size changed.
    void Advance(double seconds)
    {
        m_newFrame = false;
        plm_decode(m_plm, seconds);
        int clientW = 0, clientH = 0;
        SDL_GetWindowSizeInPixels(m_window, &clientW, &clientH);
        const bool resized = clientW != m_clientW || clientH != m_clientH;
        m_clientW = clientW;
        m_clientH = clientH;
        if ((m_newFrame || resized) && m_frames > 0) Present();
    }

private:
    static void OnFrame(plm_t*, plm_frame_t* frame, void* user)
    {
        auto self = static_cast<MpegPlayer*>(user);
        SDL_Rect rect{0, 0, self->m_width, self->m_height};
        // Planes are padded to whole macroblocks; their widths are the row pitches.
        SDL_UpdateYUVTexture(
                self->m_texture, &rect, frame->y.data, frame->y.width, frame->cb.data, frame->cb.width, frame->cr.data, frame->cr.width);
        self->m_newFrame = true;
        ++self->m_frames;
    }

    void Present()
    {
        int outW = 0, outH = 0;
        SDL_GetCurrentRenderOutputSize(m_renderer, &outW, &outH);
        screen::Rect r = ui::ImageRect(m_width, m_height, outW, outH);
        SDL_FRect dst{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right - r.left),
                static_cast<float>(r.bottom - r.top)};
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
        SDL_RenderClear(m_renderer);
        SDL_RenderTexture(m_renderer, m_texture, nullptr, &dst);
        SDL_RenderPresent(m_renderer);
    }

    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    plm_t* m_plm;
    SDL_Texture* m_texture = nullptr;
    int m_width = 0, m_height = 0;
    int m_clientW = 0, m_clientH = 0;
    bool m_newFrame = false;
    unsigned m_frames = 0;
};

// The movie's sound: decoded MP2 samples go to an SDL audio stream on the default device (shared mode, next to the
// game's Miles/DirectSound). What the device has consumed (put - still queued) is the master clock for the video.
class MovieAudio {
public:
    static constexpr double kLeadSeconds = 0.1; // decoded ahead of the clock, so the device never runs dry

    explicit MovieAudio(plm_t* plm)
    {
        if (plm_get_num_audio_streams(plm) < 1) {
            OBL_LOG("intro movie: no audio stream");
            return;
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            OBL_LOG("intro movie: SDL audio unavailable: %s - silent, wall clock", SDL_GetError());
            return;
        }
        m_rate = plm_get_samplerate(plm);
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, m_rate}; // pl_mpeg always delivers interleaved stereo floats
        m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!m_stream) {
            OBL_LOG("intro movie: SDL_OpenAudioDeviceStream failed: %s - silent, wall clock", SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return;
        }
        plm_set_audio_enabled(plm, 1);
        plm_set_audio_stream(plm, 0);
        plm_set_audio_lead_time(plm, kLeadSeconds);
        plm_set_audio_decode_callback(plm, &MovieAudio::OnSamples, this);
        SDL_ResumeAudioStreamDevice(m_stream);
        OBL_LOG("intro movie: audio %d Hz stereo via SDL stream", m_rate);
    }

    ~MovieAudio()
    {
        if (!m_stream) return;
        SDL_DestroyAudioStream(m_stream); // also closes the device it opened
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    MovieAudio(const MovieAudio&) = delete;
    MovieAudio& operator=(const MovieAudio&) = delete;

    bool Active() const { return m_stream != nullptr; }
    void Flush()
    {
        if (m_stream && !SDL_FlushAudioStream(m_stream)) OBL_LOG("intro movie: SDL_FlushAudioStream failed: %s", SDL_GetError());
    }
    bool Drained() const { return !m_stream || SDL_GetAudioStreamQueued(m_stream) <= 0; }

    double PlayedSeconds() const
    {
        if (!m_stream) return 0;
        long long played = m_putBytes - std::max(SDL_GetAudioStreamQueued(m_stream), 0);
        return static_cast<double>(played) / (static_cast<double>(m_rate) * 2 * sizeof(float));
    }

private:
    static void OnSamples(plm_t*, plm_samples_t* samples, void* user)
    {
        auto self = static_cast<MovieAudio*>(user);
        int bytes = static_cast<int>(samples->count * 2 * sizeof(float));
        if (SDL_PutAudioStreamData(self->m_stream, samples->interleaved, bytes)) self->m_putBytes += bytes;
    }

    SDL_AudioStream* m_stream = nullptr;
    int m_rate = 0;
    long long m_putBytes = 0;
};

// One playing movie: the decoder outlives the player and the audio (declared first, destroyed last).
struct MovieSession {
    MovieSession(void* gameWindow, SDL_Window* sdl, SDL_Renderer* renderer, plm_t* plm)
        : window(gameWindow), decoder(plm, &plm_destroy), player(sdl, renderer, plm), audio(plm)
    {}

    void* window;
    std::unique_ptr<plm_t, decltype(&plm_destroy)> decoder;
    MpegPlayer player;
    MovieAudio audio;
    bool audioClock = false;
    double duration = 0;
    int injectMs = -1;
    Uint64 start = 0, last = 0, lastAudioProgress = 0;
    Uint64 ended = 0; // when the decoder ended; the audio then has kDrainLimitMs to play out
    double clock = 0; // audio position already handed to the decoder
};

std::unique_ptr<MovieSession> g_session;

void StartMpeg(void* window, const char* path)
{
    SDL_Window* sdl = SdlGameWindow(window);
    SDL_Renderer* renderer = AttachSdlRenderer(window);
    if (!sdl || !renderer) {
        OBL_LOG("intro movie: no SDL renderer on the game window, skipped");
        return;
    }
    plm_t* plm = plm_create_with_filename(path);
    if (!plm || !plm_probe(plm, 5000 * 1024) || plm_get_num_video_streams(plm) < 1) {
        OBL_LOG("intro movie: cannot open %s with pl_mpeg, skipped", path);
        if (plm) plm_destroy(plm);
        return;
    }
    plm_set_audio_enabled(plm, 0); // MovieAudio enables it when SDL audio works
    plm_set_loop(plm, 0);
    auto session = std::make_unique<MovieSession>(window, sdl, renderer, plm);
    if (!session->player.Ready()) return;
    session->duration = plm_get_duration(plm);
    session->audioClock = session->audio.Active();
    OBL_LOG("intro movie: %s %dx%d %.2f fps %.2f s via pl_mpeg, %s, skip from SDL events", path, session->player.Width(),
            session->player.Height(), plm_get_framerate(plm), session->duration, "SDL texture");
    session->injectMs = SkipInjectionMs();
    session->start = session->last = session->lastAudioProgress = SDL_GetTicksNS();
    g_session = std::move(session);
}

// Records the playback and closes the decoder and the audio stream before the stats check that audio is closed.
void EndMpeg(const char* reason)
{
    MovieSession& m = *g_session;
    // The rest of the skip key/click must not reach the logo screen.
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_MOUSE_WHEEL);
    // A quit request stays pending in PumpSdlEvents: the game loop sees it next.
    g_stats.player = IntroMoviePlayer::MpegSdl;
    g_stats.frames = m.player.Frames();
    g_stats.seconds = static_cast<double>(SDL_GetTicksNS() - m.start) / SDL_NS_PER_SECOND;
    g_stats.duration = m.duration;
    g_stats.audio = m.audio.Active();
    g_stats.audioClock = m.audioClock;
    g_stats.audioSeconds = m.audio.PlayedSeconds();
    g_stats.complete = std::strcmp(reason, "complete") == 0;
    g_stats.skipped = std::strcmp(reason, "skipped by user") == 0;
    OBL_LOG("intro movie: finished (%s), %u frames in %.2f s, audio %.2f s (%s clock)", reason, g_stats.frames, g_stats.seconds,
            g_stats.audioSeconds, m.audioClock ? "audio" : "wall");
    g_session.reset();
    g_stats.audioOpen = SDL_WasInit(SDL_INIT_AUDIO) != 0;
}

// One pass of the movie: events, skip, clock, decode and present. Null while it plays, else why it ended.
const char* AdvanceMpeg(MovieSession& m)
{
    const char* reason = nullptr;
    SdlPumpResult pump = PumpSdlEvents();
    if (pump.quit) reason = "quit requested";
    SDL_Event event;
    // Input events only; focus/window events stay queued for the game's input adapter.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_KEY_DOWN, SDL_EVENT_MOUSE_WHEEL) > 0) {
        if (!reason && IsSkipEvent(event)) {
            OBL_LOG("intro movie: skip event %04X", event.type);
            reason = "skipped by user";
        }
    }
    if (reason) return reason;
    if (!SdlGameWindow(m.window)) return "game window destroyed";
    Uint64 now = SDL_GetTicksNS();
    if (m.injectMs >= 0 && now - m.start >= static_cast<Uint64>(m.injectMs) * SDL_NS_PER_MS) {
        OBL_LOG("intro movie: injecting Escape after %d ms (OBL_MOVIE_SKIP_MS)", m.injectMs);
        PushEscape();
        m.injectMs = -1;
    }
    // A stalled loop (window dragged, debugger, hidden browser tab) continues where it stopped instead of racing to catch
    // up; with audio the device simply runs dry and its position waits as well.
    double dt = std::min(static_cast<double>(now - m.last) / SDL_NS_PER_SECOND, 0.1);
    m.last = now;
    plm_t* plm = m.decoder.get();
    if (m.audioClock) {
        double played = m.audio.PlayedSeconds();
        if (played > m.clock) {
            dt = played - m.clock;
            m.clock = played;
            m.lastAudioProgress = now;
        } else if (now - m.lastAudioProgress > 500 * SDL_NS_PER_MS && !plm_has_ended(plm)) {
            OBL_LOG("intro movie: audio position stuck at %.2f s, switching to the wall clock", played);
            m.audioClock = false;
        } else {
            dt = 0;
        }
    }
    m.player.Advance(dt);
    if (!plm_has_ended(plm)) return nullptr;
    if (m.ended == 0) {
        m.ended = now;
        // The decoder has handed over its last samples. A stream that converts to another device rate (a browser's
        // AudioContext runs at 48 kHz) keeps its last frames until it is flushed, so it would never count as drained.
        m.audio.Flush();
    }
    // A device that stops consuming must not hold the game on the movie's last frame.
    if (m.audio.Drained()) return "complete";
    return now - m.ended > kDrainLimitMs * SDL_NS_PER_MS ? "complete" : nullptr;
}

} // namespace

const IntroMovieStats& GetIntroMovieStats()
{
    return g_stats;
}

void IntroMovieStart(void* window, const char* path)
{
    if (!config::Get().playIntroMovie) {
        OBL_LOG("intro movie: skipped (PlayMovie=0)");
        return;
    }
    // The movie's 640x480 placement in full screen; the renderer does not exist yet at this point.
    if (!config::Get().windowed) PlaceGameWindow(window, 640, 480);
    StartMpeg(window, path);
}

bool IntroMovieFrame()
{
    if (!g_session) return false;
    if (const char* reason = AdvanceMpeg(*g_session)) {
        EndMpeg(reason);
        return false;
    }
#ifndef __EMSCRIPTEN__
    // The desktop loop calls the next pass at once; the browser calls one per animation frame.
    SDL_DelayNS(2 * SDL_NS_PER_MS);
#endif
    return true;
}

void IntroMovieFinish()
{
    if (g_session) EndMpeg("closed");
    ++g_stats.destructCalls;
}

} // namespace obl::platform
