#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A record of the animation calls of a guest function. The sprite animation (the guest's PlayAnimation, starting the named sprite animation) changes the guest,
// the sprite and the sound system, so while `active` is set the calls are listed and skipped. Nothing sets it. Used by src/game/objects/guest_animation.cpp.
namespace obl::object::guest_anim_record {

// The length every skipped PlayAnimation call returns, a fixed number so that the delayed message carries something to compare.
constexpr float kRecordedAnimationLength = 1.5f;

struct AnimCall {
    bool board; // false: the guest's PlayAnimation, true: starting the named sprite animation
    uintptr_t object;
    std::string name;
    uint32_t a, b, c, d; // the remaining arguments, floats as bits
    bool operator==(const AnimCall&) const = default;
};

struct AnimRecording {
    bool active = false;
    bool replay = false;
    std::vector<AnimCall> recorded, replayed;
    void Add(AnimCall call) { (replay ? replayed : recorded).push_back(std::move(call)); }
};

inline AnimRecording& Recording()
{
    static AnimRecording recording;
    return recording;
}

} // namespace obl::object::guest_anim_record
