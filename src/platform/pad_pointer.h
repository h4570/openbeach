#pragma once

// A thumb stick as a mouse, without SDL calls or game state (the handheld build, docs/build/handheld.md): the stick's two axes in,
// pointer motion in client pixels out. Speed follows the deflection with a curve, so that a small push aims precisely and a full
// push crosses the screen in about a second.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace obl::platform {

// Where the backend reads a stick instead of a mouse: the handheld build (OBL_HANDHELD, host_linux/CMakeLists.txt).
constexpr bool kHandheld =
#ifdef OBL_HANDHELD
        true;
#else
        false;
#endif

// SDL joystick axes run from -32768 to 32767.
constexpr double kPadAxisRange = 32767.0;
constexpr double kPadDeadZone = 0.15; // of the full deflection: a worn stick rests off centre
constexpr double kPadMaxSpeed = 1100.0; // pixels per second at full deflection and 100 % speed (the Video options slider)
constexpr double kPadMinPercent = 10.0; // the slider cannot stop the cursor: it could not be moved to the slider again

class PadPointer {
public:
    struct Delta {
        double x, y;
    };

    // `axisX`/`axisY` raw SDL values, `seconds` since the last call, `speedPercent` the setting. The result is the motion of this step.
    Delta Step(int axisX, int axisY, double seconds, double speedPercent = 100.0) const
    {
        double x = axisX / kPadAxisRange, y = axisY / kPadAxisRange;
        const double length = std::sqrt(x * x + y * y);
        if (length <= kPadDeadZone) return {0, 0};
        // Radial dead zone rescaled to 0..1, then squared: fine control near the centre, full speed at the rim.
        const double magnitude = std::min(1.0, (length - kPadDeadZone) / (1.0 - kPadDeadZone));
        const double percent = std::clamp(speedPercent, kPadMinPercent, 100.0);
        const double speed = kPadMaxSpeed * percent / 100.0 * magnitude * magnitude * seconds;
        return {x / length * speed, y / length * speed};
    }
};

// Keeps the fractions a step cannot show as whole pixels, so that slow motion is not lost.
class PadAccumulator {
public:
    int Add(double value)
    {
        m_rest += value;
        const double whole = std::trunc(m_rest);
        m_rest -= whole;
        return static_cast<int>(whole);
    }
    void Reset() { m_rest = 0; }

private:
    double m_rest = 0;
};

} // namespace obl::platform
