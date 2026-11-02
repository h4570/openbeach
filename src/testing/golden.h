#pragma once

#include "core/char16.h"
#include <cstdint>
#include <string>
#include <vector>

namespace obl::test {

// Golden screenshots (docs/testing/golden-images.md): `expect-shot <name> [tol] [max-percent]` compares the next presented frame
// with <game dir>/golden/<name>.png. OBL_UPDATE_GOLDEN=1 (autotest.ps1 -UpdateGolden) records the frame as the new golden.
void InitGolden(const Text16& gameDirectory, const Text16& captureDirectory);

// Script side: true once the requested frame has been compared (or the request timed out); the script waits until then.
bool StepExpectShot(const std::vector<std::string>& args);

bool GoldenShotPending();

// Present side: called with the presented 16 bpp RGB555 back buffer.
void OnPresentGolden(const uint16_t* bits, int width, int height, int pitchPixels);

} // namespace obl::test
