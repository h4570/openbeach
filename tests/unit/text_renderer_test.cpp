#include "core/char16.h"
#include "unit_test.h"
#include "game/ui/text_renderer.h"
#include <array>
#include <memory>
using namespace obl;

OBL_TEST_CASE("text renderer: negative kerning uses stride 255 and 16-bit metric advance")
{
    auto font = std::make_unique<ui::BitmapFont>();
    font->m_glyphs[u'A'] = {0, 0, 1, 1};
    font->m_glyphs[u'V'] = {0, 0, 4, 1};
    font->m_kerning[u'A' * 255 + u'V'] = -2;
    font->m_kerning[u'A' * 256 + u'V'] = 7;
    OBL_CHECK_EQ(text_renderer::Measure(*font, u"AV"), 65539u);
    OBL_CHECK_EQ(text_renderer::Measure(*font, u""), 0u);
}
OBL_TEST_CASE("text renderer: word wrapping keeps trailing spaces and ignores tilde in metrics")
{
    auto font = std::make_unique<ui::BitmapFont>();
    for (auto& glyph : font->m_glyphs) glyph = {0, 0, 4, 6};
    OBL_CHECK_EQ(text_renderer::CountLines(*font, u"aa bb", 12, 0), 2);
    OBL_CHECK_EQ(text_renderer::WrappedWidth(*font, u"aa bb", 12, 0), 8u);
    OBL_CHECK_EQ(text_renderer::CountLines(*font, u"abcdef", 1, 0), 1);
    OBL_CHECK_EQ(text_renderer::CountLines(*font, u"a~b", 12, 0), 1);
    OBL_CHECK_EQ(text_renderer::CountLines(*font, u"", 0, 8), 1);
    OBL_CHECK_EQ(text_renderer::CountLines(*font, u"   ", 0, 8), 1);
    std::array<Char16, 32> text{u'a', u'b', u'c', u'd', u'e', 0};
    text_renderer::Truncate(*font, text.data(), 16);
    OBL_CHECK(Text16(text.data()) == u"a...");
    text_renderer::Truncate(*font, text.data(), 1);
    OBL_CHECK(Text16(text.data()) == u"...");
}
OBL_TEST_CASE("text renderer: forced line break and font-height spacing")
{
    auto font = std::make_unique<ui::BitmapFont>();
    font->m_size = 7;
    for (auto& glyph : font->m_glyphs) glyph = {0, 0, 4, 6};
    ui::TextRenderer renderer{};
    renderer.m_font = font.get();
    renderer.m_width = 100;
    renderer.m_height = 50;
    std::vector<Text16> lines;
    std::vector<int> ys;
    text_renderer::DrawWrapped(renderer, 0, 0, u"abc~def", [&](int, int y, const Char16* line) {
        lines.emplace_back(line);
        ys.push_back(y);
    });
    OBL_REQUIRE(lines.size() == 2);
    OBL_CHECK(lines[0] == u"abc");
    OBL_CHECK(lines[1] == u"def");
    OBL_CHECK_EQ(ys[1], 10);
}
OBL_TEST_CASE("text renderer: outline black wins and 32-bit destination depth survives")
{
    ui::TextRenderer renderer{};
    renderer.m_outlined = 1;
    const uint8_t src[]{0, 1, 2, 3};
    std::array<uint32_t, 4> dest;
    dest.fill(0x1234abcd);
    text_renderer::BlitGlyph(renderer, src, 4, dest.data(), 4, 0, 0, 0, 0, 4, 1, 4, 0x5555);
    OBL_CHECK_EQ(dest[0], 0x1234abcdu);
    OBL_CHECK_EQ(dest[1], 0x1234ffffu);
    OBL_CHECK_EQ(dest[2], 0x12340000u);
    OBL_CHECK_EQ(dest[3], 0x12340000u);
}
