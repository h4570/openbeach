#include "game/objects/mod_script_functions.h"

#include <cmath>

// The checks of the generic functions that need no game state; the functions themselves are in mod_script_calls.cpp (the game library),
// so the unit tests can link these.
namespace obl::mod_script {

std::string CheckRadius(double radius)
{
    if (!std::isfinite(radius) || radius < 0 || radius > kMaxRadius)
        return "the radius must be between 0 and " + std::to_string(kMaxRadius) + " tiles";
    return {};
}

std::string CheckMoney(double amount)
{
    if (!std::isfinite(amount) || amount <= 0 || amount > kMaxMoney)
        return "the amount must be above 0 and at most " + std::to_string(static_cast<int>(kMaxMoney));
    return {};
}

std::string CheckUnitDelta(double delta)
{
    if (!std::isfinite(delta) || delta < -1 || delta > 1) return "the change must be between -1 and 1";
    return {};
}

float AddClamped(float value, float delta)
{
    const float sum = value + delta;
    return sum < 0.0f ? 0.0f : sum > 1.0f ? 1.0f : sum;
}

} // namespace obl::mod_script
