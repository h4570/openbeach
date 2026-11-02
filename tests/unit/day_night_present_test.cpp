
#include "unit_test.h"

#include "game/ui/day_night_present.h"

#include <array>
#include <cstring>
#include <vector>

using namespace obl;
using namespace obl::day_night;

namespace {

constexpr int kLevels = 3;

// Level 0 is the identity (every channel entry is its own value, already shifted into place); level 1 turns every channel to 0;
// level 2 maps every channel to its maximum.
struct Tones {
    std::vector<uint16_t> words = std::vector<uint16_t>(kLevels * kToneTableWords);
    explicit Tones(bool rgb555)
    {
        const int greenBits = rgb555 ? 5 : 6;
        const int redShift = greenBits + 5;
        for (int level = 0; level < kLevels; ++level) {
            uint16_t* t = words.data() + level * kToneTableWords;
            for (int i = 0; i < 32; ++i) {
                t[i] = level == 0 ? i << redShift : level == 1 ? 0 : 31 << redShift;
                t[(rgb555 ? 0x40 : 0x60) + i] = level == 0 ? i : level == 1 ? 0 : 31;
            }
            for (int i = 0; i < (1 << greenBits); ++i) t[0x20 + i] = level == 0 ? i << 5 : level == 1 ? 0 : ((1 << greenBits) - 1) << 5;
        }
    }
};

// Scene words with one pixel of slack on each side (the loops read the left neighbour) and room for shimmer offsets.
struct Scene {
    std::vector<uint32_t> words;
    int width, height;
    Scene(int w, int h) : words(static_cast<size_t>(w) * h + 600, 0), width(w), height(h) {}
    uint32_t* Base() { return words.data() + 1; }
    uint32_t& At(int x, int y) { return Base()[y * width + x]; }
};

Frame MakeFrame(Scene& scene, const Tones& tones, bool rgb555, bool doubled)
{
    Frame f;
    f.width = scene.width;
    f.height = scene.height;
    f.rgb555 = rgb555;
    f.doubled = doubled;
    f.scene = scene.Base();
    f.tones = tones.words.data();
    return f;
}

uint16_t Out(const std::vector<uint8_t>& bytes, int x, int y, uint32_t pitch)
{
    uint16_t v;
    std::memcpy(&v, bytes.data() + y * pitch + x * 2, 2);
    return v;
}

} // namespace

OBL_TEST_CASE("day-night: plain tone is the identity with the identity table, 555 and 565")
{
    for (bool rgb555 : {true, false}) {
        const Tones tones(rgb555);
        Scene scene(4, 2);
        const uint16_t pixels[8] = {0x0000, 0x7fff, 0x1234, 0x0421, 0x03e0, 0x7c00, 0x001f, 0x0abc};
        for (int i = 0; i < 8; ++i) scene.Base()[i] = (0xdead0000u) | pixels[i]; // the depth half never reaches the output
        std::vector<uint8_t> out(2 * 4 * 2 + 16, 0xee);
        Present(Variant::Plain, MakeFrame(scene, tones, rgb555, false), out.data(), 8);
        for (int i = 0; i < 8; ++i) OBL_CHECK(Out(out, i % 4, i / 4, 8) == pixels[i]);
    }
}

OBL_TEST_CASE("day-night: the light buffer byte picks the tone level, pixel by pixel")
{
    const Tones tones(true);
    Scene scene(4, 1);
    for (int i = 0; i < 4; ++i) scene.Base()[i] = 0x7fff;
    const uint8_t light[8] = {0, 1, 0, 2, 0, 0, 0, 0};
    Frame f = MakeFrame(scene, tones, true, false);
    f.light = light;
    std::vector<uint8_t> out(8 + 8, 0);
    Present(Variant::Lightmap, f, out.data(), 8);
    OBL_CHECK(Out(out, 0, 0, 8) == 0x7fff);
    OBL_CHECK(Out(out, 1, 0, 8) == 0x0000);
    OBL_CHECK(Out(out, 2, 0, 8) == 0x7fff);
    OBL_CHECK(Out(out, 3, 0, 8) == 0x7fff); // level 2 maps to the maximum
}

OBL_TEST_CASE("day-night: zoom 0 doubles each pixel, blends with the left neighbour and repeats the row")
{
    const Tones tones(true);
    Scene scene(4, 2); // the first two scene columns of each row are used
    scene.Base()[-1] = 0x7fff; // outside the scene: must not reach the output
    scene.Base()[0] = 0x7c00; // red
    scene.Base()[1] = 0x001f; // blue
    Frame f = MakeFrame(scene, tones, true, true);
    std::vector<uint8_t> out(2 * 8 + 16, 0);
    Present(Variant::Plain, f, out.data(), 8);
    // Column 0: the first pixel has no left neighbour, so it blends with itself; the plain tone follows.
    OBL_CHECK(Out(out, 0, 0, 8) == 0x7c00);
    OBL_CHECK(Out(out, 1, 0, 8) == 0x7c00);
    // Column 1: half red of the left neighbour plus half blue.
    OBL_CHECK(Out(out, 2, 0, 8) == 0x3c0f);
    OBL_CHECK(Out(out, 3, 0, 8) == 0x001f);
    for (int x = 0; x < 4; ++x) OBL_CHECK(Out(out, x, 1, 8) == Out(out, x, 0, 8));
    OBL_CHECK(PresentedRows(f) == 1);
}

OBL_TEST_CASE("day-night: zoom 0 lightmap lights the pixel with the next byte and its neighbour with its own")
{
    const Tones tones(true);
    Scene scene(2, 2);
    scene.Base()[-1] = 0x7fff;
    scene.Base()[0] = 0x7fff;
    const uint8_t light[4] = {0, 1, 0, 0}; // pixel 0 sees byte 1 (black), its left neighbour byte 0
    Frame f = MakeFrame(scene, tones, true, true);
    f.light = light;
    std::vector<uint8_t> out(2 * 4 + 16, 0);
    Present(Variant::Lightmap, f, out.data(), 4);
    OBL_CHECK(Out(out, 1, 0, 4) == 0x0000); // plain tone of the black pixel
    OBL_CHECK(Out(out, 0, 0, 4) == 0x3def); // half of the lit left neighbour, 0x7fff halved per channel
}

OBL_TEST_CASE("day-night: the heat shimmer offsets the two pixels of a pair independently and the row advances and wraps")
{
    const Tones tones(true);
    Scene scene(4, 25);
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 4; ++x) scene.At(x, y) = static_cast<uint32_t>(y * 4 + x + 1) << 4 & 0x7fff;
    std::vector<uint8_t> shimmer(kShimmerBytes, 0);
    for (int i = 0; i < 32; ++i) shimmer[i] = static_cast<uint8_t>(i % 3);
    Frame f = MakeFrame(scene, tones, true, false);
    f.shimmer = shimmer.data();
    std::vector<uint8_t> out(25 * 8 + 16, 0);
    Present(Variant::NormalWeather, f, out.data(), 8);
    // x = 0: column (0 - 4) & 31 = 28; the first pixel uses entry 29 (29 % 3 = 2) and reads scene pixel 2, the second entry 28 (1) and reads pixel 1 + 1.
    OBL_CHECK(Out(out, 0, 0, 8) == (scene.At(2, 0) & 0xffff));
    OBL_CHECK(Out(out, 1, 0, 8) == (scene.Base()[1 + 1] & 0xffff));
    // Row 24 reuses the first shimmer row after 24 advances (0x300 / 0x20).
    OBL_CHECK(Out(out, 0, 24, 8) == (scene.Base()[24 * 4 + 2] & 0xffff));
}

OBL_TEST_CASE("day-night: variant choice, presented rows and light buffer clean-up")
{
    OBL_CHECK(ChooseVariant(0, false) == Variant::NormalWeather);
    OBL_CHECK(ChooseVariant(0, true) == Variant::NormalWeather);
    OBL_CHECK(ChooseVariant(1, true) == Variant::Lightmap);
    OBL_CHECK(ChooseVariant(5, false) == Variant::Plain);
    OBL_CHECK(ClearsLightBuffer(Variant::Lightmap, false));
    OBL_CHECK(ClearsLightBuffer(Variant::Lightmap, true));
    OBL_CHECK(ClearsLightBuffer(Variant::NormalWeather, true));
    OBL_CHECK(!ClearsLightBuffer(Variant::NormalWeather, false));
    OBL_CHECK(!ClearsLightBuffer(Variant::Plain, true));
    Frame f;
    f.width = 8;
    f.height = 11;
    f.bottomTrim = 1;
    OBL_CHECK(PresentedRows(f) == 10);
    f.doubled = true;
    OBL_CHECK(PresentedRows(f) == 5);
    f.bottomTrim = 20;
    OBL_CHECK(PresentedRows(f) == 0);
    f.bottomTrim = 0;
    f.width = 0;
    OBL_CHECK(PresentedRows(f) == 0);
}
