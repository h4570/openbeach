#pragma once

#include <cstdint>
#include <vector>

namespace obl {

// The count of the game's rand() draws, and record/replay of those draws. One thread; recordings nest as a stack of frames:
// a recording that starts while another one records (the recorded run is still going) opens its own frame on top. A draw of the source is
// recorded in every frame that is not paused at or above it, so the outer recording still holds the inner run's draws, while an
// inner replay is served from its own frame alone and reaches neither the source nor the outer frames. Nothing can start on top of a
// replay: the outer run goes on with draws that were already made. Replay never calls the source, even on overdraw.
// End/Cancel close the top frame and leave the logical counter and the source at the end of that frame's recorded run.
class RandTape {
public:
    // false while the top frame replays (the caller runs inside a replay).
    bool BeginRecord()
    {
        if (Replaying()) return false;
        Frame frame;
        frame.start = m_calls;
        m_frames.push_back(std::move(frame));
        return true;
    }
    bool BeginReplay()
    {
        if (m_frames.empty() || m_frames.back().mode != Mode::Record) return false;
        Frame& frame = m_frames.back();
        frame.end = m_calls;
        m_calls = frame.start;
        frame.used = 0;
        frame.mode = Mode::Replay;
        return true;
    }
    bool End()
    {
        if (!Replaying()) return false;
        bool same = m_frames.back().used == m_frames.back().values.size();
        Cancel();
        return same;
    }
    void Cancel()
    {
        if (m_frames.empty()) return;
        if (m_frames.back().mode == Mode::Replay) m_calls = m_frames.back().end;
        m_frames.pop_back();
    }
    bool ResetCalls()
    {
        if (!m_frames.empty()) return false;
        m_calls = 0;
        return true;
    }
    // Draws made while paused (the cascade a delivered message triggers, a helper the replay does not run again) still reach the source and the
    // logical counter but are not recorded, so the replay is not asked to reproduce them. Only a recorded run pauses; nests. A pause
    // applies to the top frame and every frame below it: what the inner replay does not reproduce, the outer one does not either.
    void Pause()
    {
        if (!m_frames.empty()) ++m_frames.back().paused;
    }
    void Resume()
    {
        if (!m_frames.empty() && m_frames.back().paused) --m_frames.back().paused;
    }
    template<class Source> int Draw(Source source)
    {
        ++m_calls;
        if (Replaying()) {
            Frame& frame = m_frames.back();
            size_t index = frame.used++;
            return index < frame.values.size() ? frame.values[index] : 0;
        }
        int value = source();
        for (size_t i = m_frames.size(); i-- > 0;) {
            if (m_frames[i].paused) break; // this frame and all below it skip the draw
            m_frames[i].values.push_back(value);
        }
        return value;
    }
    uint64_t Calls() const { return m_calls; }
    // Open frames (0 = idle).
    size_t Depth() const { return m_frames.size(); }
    bool Replaying() const { return !m_frames.empty() && m_frames.back().mode == Mode::Replay; }

private:
    enum class Mode { Record, Replay };
    struct Frame {
        Mode mode = Mode::Record;
        uint64_t start = 0, end = 0;
        int paused = 0;
        size_t used = 0;
        std::vector<int> values;
    };
    uint64_t m_calls = 0;
    std::vector<Frame> m_frames;
};

} // namespace obl
