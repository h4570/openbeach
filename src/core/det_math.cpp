// Ported from fdlibm 5.3 (e_acos.c, s_sin.c, k_sin.c, k_cos.c, e_rem_pio2.c):
//
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// Constants are written as bit patterns so no decimal parsing is involved; the word macros of fdlibm became bit_cast.
// rem_pio2 always takes the refining branch of the medium path (fdlibm skips it for some arguments via its npio2_hw
// table); this is part of the specification here and pinned by the unit tests.

#include "core/det_math.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace obl::det_math {
namespace {

constexpr double D(uint64_t bits)
{
    return std::bit_cast<double>(bits);
}
int32_t High(double x)
{
    return static_cast<int32_t>(std::bit_cast<uint64_t>(x) >> 32);
}
uint32_t Low(double x)
{
    return static_cast<uint32_t>(std::bit_cast<uint64_t>(x));
}
double WithLowZero(double x)
{
    return std::bit_cast<double>(std::bit_cast<uint64_t>(x) & 0xffffffff00000000ull);
}

constexpr double kPi = D(0x400921fb54442d18), kPio2Hi = D(0x3ff921fb54442d18), kPio2Lo = D(0x3c91a62633145c07);
constexpr double kPS0 = D(0x3fc5555555555555), kPS1 = D(0xbfd4d61203eb6f7d), kPS2 = D(0x3fc9c1550e884455), kPS3 = D(0xbfa48228b5688f3b),
                 kPS4 = D(0x3f49efe07501b288), kPS5 = D(0x3f023de10dfdf709);
constexpr double kQS1 = D(0xc0033a271c8a2d4b), kQS2 = D(0x40002ae59c598ac8), kQS3 = D(0xbfe6066c1b8d0159), kQS4 = D(0x3fb3b8c5b12e9282);

double AcosP(double z)
{
    return z * (kPS0 + z * (kPS1 + z * (kPS2 + z * (kPS3 + z * (kPS4 + z * kPS5)))));
}
double AcosQ(double z)
{
    return 1.0 + z * (kQS1 + z * (kQS2 + z * (kQS3 + z * kQS4)));
}

constexpr double kS1 = D(0xbfc5555555555549), kS2 = D(0x3f8111111110f8a6), kS3 = D(0xbf2a01a019c161d5), kS4 = D(0x3ec71de357b1fe7d),
                 kS5 = D(0xbe5ae5e68a2b9ceb), kS6 = D(0x3de5d93a5acfd57c);
constexpr double kC1 = D(0x3fa555555555554c), kC2 = D(0xbf56c16c16c15177), kC3 = D(0x3efa01a019cb1590), kC4 = D(0xbe927e4f809c52ad),
                 kC5 = D(0x3e21ee9ebdb4b1c4), kC6 = D(0xbda8fae9be8838d4);

// sin(x + y) for |x| <= pi/4, y the tail of x (hasTail false: y is 0 and the shorter fdlibm form runs).
double KernelSin(double x, double y, bool hasTail)
{
    if ((High(x) & 0x7fffffff) < 0x3e400000 && static_cast<int>(x) == 0) return x; // |x| < 2^-27
    const double z = x * x, v = z * x;
    const double r = kS2 + z * (kS3 + z * (kS4 + z * (kS5 + z * kS6)));
    if (!hasTail) return x + v * (kS1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * kS1);
}

// cos(x + y) for |x| <= pi/4.
double KernelCos(double x, double y)
{
    const int32_t ix = High(x) & 0x7fffffff;
    if (ix < 0x3e400000 && static_cast<int>(x) == 0) return 1.0;
    const double z = x * x;
    const double r = z * (kC1 + z * (kC2 + z * (kC3 + z * (kC4 + z * (kC5 + z * kC6)))));
    if (ix < 0x3fd33333) return 1.0 - (0.5 * z - (z * r - x * y));
    const double qx = ix > 0x3fe90000 ? 0.28125 : std::bit_cast<double>(static_cast<uint64_t>(ix - 0x00200000) << 32);
    const double hz = 0.5 * z - qx, a = 1.0 - qx;
    return a - (hz - (z * r - x * y));
}

constexpr double kInvPio2 = D(0x3fe45f306dc9c883);
constexpr double kPio2_1 = D(0x3ff921fb54400000), kPio2_1t = D(0x3dd0b4611a626331);
constexpr double kPio2_2 = D(0x3dd0b4611a600000), kPio2_2t = D(0x3ba3198a2e037073);
constexpr double kPio2_3 = D(0x3ba3198a2e000000), kPio2_3t = D(0x397b839a252049c1);

// x - n*pi/2 = y[0] + y[1] for pi/4 < |x| < kSinMaxArgument; returns n.
int RemPio2(double x, double* y)
{
    const int32_t hx = High(x), ix = hx & 0x7fffffff;
    const double t = std::fabs(x);
    const int n = static_cast<int>(t * kInvPio2 + 0.5);
    const double count = static_cast<double>(n);
    double r = t - count * kPio2_1;
    double w = count * kPio2_1t; // first round good to 85 bits
    const int32_t j = ix >> 20;
    y[0] = r - w;
    int32_t i = j - ((High(y[0]) >> 20) & 0x7ff);
    if (i > 16) { // second iteration, good to 118 bits
        double u = r;
        w = count * kPio2_2;
        r = u - w;
        w = count * kPio2_2t - ((u - r) - w);
        y[0] = r - w;
        i = j - ((High(y[0]) >> 20) & 0x7ff);
        if (i > 49) { // third iteration, 151 bits
            u = r;
            w = count * kPio2_3;
            r = u - w;
            w = count * kPio2_3t - ((u - r) - w);
            y[0] = r - w;
        }
    }
    y[1] = (r - y[0]) - w;
    if (hx < 0) {
        y[0] = -y[0];
        y[1] = -y[1];
        return -n;
    }
    return n;
}

} // namespace

double Acos(double x)
{
    const int32_t hx = High(x), ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) { // |x| >= 1
        if (((ix - 0x3ff00000) | static_cast<int32_t>(Low(x))) == 0) return hx > 0 ? 0.0 : kPi + 2.0 * kPio2Lo;
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (ix < 0x3fe00000) { // |x| < 0.5
        if (ix <= 0x3c600000) return kPio2Hi + kPio2Lo; // |x| < 2^-57
        const double z = x * x;
        const double r = AcosP(z) / AcosQ(z);
        return kPio2Hi - (x - (kPio2Lo - x * r));
    }
    if (hx < 0) { // x < -0.5
        const double z = (1.0 + x) * 0.5;
        const double p = AcosP(z), q = AcosQ(z);
        const double s = std::sqrt(z);
        const double r = p / q;
        const double w = r * s - kPio2Lo;
        return kPi - 2.0 * (s + w);
    }
    // x > 0.5
    const double z = (1.0 - x) * 0.5;
    const double s = std::sqrt(z);
    const double df = WithLowZero(s);
    const double c = (z - df * df) / (s + df);
    const double p = AcosP(z), q = AcosQ(z);
    const double r = p / q;
    const double w = r * s + c;
    return 2.0 * (df + w);
}

double Sin(double x)
{
    const int32_t ix = High(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return KernelSin(x, 0.0, false); // |x| ~< pi/4
    if (!(std::fabs(x) < kSinMaxArgument)) return std::numeric_limits<double>::quiet_NaN();
    double y[2];
    switch (RemPio2(x, y) & 3) {
    case 0: return KernelSin(y[0], y[1], true);
    case 1: return KernelCos(y[0], y[1]);
    case 2: return -KernelSin(y[0], y[1], true);
    default: return -KernelCos(y[0], y[1]);
    }
}

void BuildSineTable(float* table)
{
    for (int i = 0; i < kSineTableSize; ++i)
        table[i] = static_cast<float>(Sin(static_cast<double>(i) * 0.03125 * static_cast<double>(0.017453292f)));
}

} // namespace obl::det_math
