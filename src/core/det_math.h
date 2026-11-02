#pragma once

// Deterministic transcendental math for the simulation: results depend only on IEEE double `+ - * /` and `sqrt`, so
// they are bit-identical on double at 53-bit precision (the game's control word), SSE2 and any later x64 build, unlike the
// CRT `acos`/`sin` (which differ between the MSVC and glibc CRTs) or the hardware sine instruction (docs/testing/determinism.md).
//
// Contract for the implementation file: no FMA contraction (`-ffp-contract=off`, MSVC `/fp:precise`), no reassociation
// (`-ffast-math`), and the evaluation order of the fdlibm sources it follows kept exactly. Changing any result bit
// changes replay hashes; the pinned vectors in tests/unit/det_math_test.cpp catch it.

namespace obl::det_math {

// fdlibm 5.3 `__ieee754_acos` (error < 1 ulp). acos(1) = +0, acos(-1) = pi; NaN for NaN or |x| > 1.
double Acos(double x);

// fdlibm 5.3 `sin` with the medium-range argument reduction only: |x| must be below kSinMaxArgument (larger arguments
// return NaN instead of using the Payne-Hanek reduction, which the simulation never needs).
inline constexpr double kSinMaxArgument = 843314.0; // 2^19 * pi/2, the end of fdlibm's medium path
double Sin(double x);

// The fast sine table: the sine of every 1/32 degree as a float, with the game's
// argument expression (i * 0.03125 * double(0.017453292f)) and det_math::Sin instead of the hardware sine.
inline constexpr int kSineTableSize = 11520;
void BuildSineTable(float* table);

} // namespace obl::det_math
