#include "importer/src/readers/pis.h"

#include "core/ascii_case.h"

#include <cstring>

namespace obl::pis {
namespace {

// `char` is compared as signed (CMP CL,0x20 / JLE); bytes >= 0x80 are therefore "below" every limit.
bool IsToken(char c)
{
    return c > ' ' && c < 'z';
}

template<class Visit> int Walk(const char* text, int length, Visit&& visit)
{
    // After a key without a closing quote the scan steps one byte past the buffer and reads garbage;
    // clamping to the buffer end only changes that undefined case.
    auto clamp = [&](int off) { return off > length ? length : off; };
    int count = 0;
    int off = SkipSeparators(text, length);
    while (HasKey(text + off, length - off)) {
        off = clamp(off + SkipToKey(text + off, length - off));
        int keyAt = off;
        off = clamp(off + FindKeyEnd(text + off, length - off));
        off = clamp(off + SkipSeparators(text + off, length - off));
        int tokenAt = off;
        int tokenLength = TokenLength(text + off, length - off);
        visit(keyAt, tokenAt, tokenLength);
        off = clamp(off + tokenLength);
        off = clamp(off + SkipSeparators(text + off, length - off));
        ++count;
    }
    return count;
}

} // namespace

int QuotedLength(const char* text, int length)
{
    int quotes = 2;
    for (int i = 0; i < length; ++i) {
        if (text[i] == '"' && --quotes == 0) return i + 1;
    }
    return 0;
}

int FindKeyEnd(const char* text, int length)
{
    int i = 1;
    if (length <= 1) return i;
    while (text[i] != '\'') {
        if (++i >= length) return i;
    }
    return i + 1;
}

int TokenLength(const char* text, int length)
{
    if (text[0] == '"') {
        int i = 1;
        if (length <= 1) return i;
        do {
            if (text[i] == '"') return i + 1;
        } while (++i < length);
        return i;
    }
    int i = 0;
    while (i < length && IsToken(text[i])) ++i;
    return i;
}

int SkipSeparators(const char* text, int length)
{
    int i = 0;
    while (i < length && !IsToken(text[i])) ++i;
    return i;
}

bool HasKey(const char* text, int length)
{
    int quotes = 2;
    for (int i = 0; i < length; ++i) {
        if (text[i] == '\'' && --quotes == 0) return true;
    }
    return false;
}

int SkipToKey(const char* text, int length)
{
    int i = 0;
    while (i < length) {
        if (text[i] == '\'') return i;
        ++i;
        if (text[i] == '/') {
            do {
                if (i >= length) return length;
                ++i;
            } while (text[i] != '\r');
        }
    }
    return length;
}

double ParseNumber(const char* text)
{
    double sign = text[0] == '-' ? -1.0 : 1.0;
    int i = text[0] == '-' ? 1 : 0;
    double value = 0.0;
    while (text[i] >= '0' && text[i] <= '9') value = value * 10.0 + (text[i++] - '0');
    if (text[i] == '.') {
        double scale = 1.0;
        for (++i; text[i] >= '0' && text[i] <= '9'; ++i) {
            scale *= 0.1f; // the constant is a float, so the scale is 0.1f widened, not 0.1
            value = (text[i] - '0') * scale + value;
        }
    }
    return value * sign;
}

int ClassifyValue(const char* text, int length)
{
    char c = text[0];
    if ((c >= '0' && c <= '9') || c == '.' || c == '-') return Number;
    return QuotedLength(text, length) != 0 ? String : None;
}

int CountEntries(const char* text, int length)
{
    return Walk(text, length, [](int, int, int) {});
}

std::string KeyText(const char* text, int length)
{
    int end = FindKeyEnd(text, length);
    // This allocates end-1 bytes and writes at end-2, which underflows for end == 1; only reachable on garbage input.
    return end < 2 ? std::string() : std::string(text + 1, static_cast<size_t>(end - 2));
}

Entry ValueEntry(const char* text, int length)
{
    Entry entry;
    entry.type = ClassifyValue(text, length);
    if (entry.type == String) {
        int quoted = QuotedLength(text, length);
        if (quoted != 0) {
            entry.hasString = true;
            entry.string.assign(text + 1, static_cast<size_t>(quoted - 2));
        }
    } else if (entry.type == Number) {
        entry.hasNumber = true;
        entry.number = static_cast<float>(ParseNumber(text));
    }
    return entry;
}

std::vector<Entry> ParseEntries(const char* text, int length)
{
    std::vector<Entry> entries;
    Walk(text, length, [&](int keyAt, int tokenAt, int tokenLength) {
        Entry entry = ValueEntry(text + tokenAt, tokenLength);
        entry.key = KeyText(text + keyAt, length - keyAt);
        entries.push_back(std::move(entry));
    });
    return entries;
}

namespace {

const RawEntry* FindLast(const RawEntry* entries, int count, const char* key)
{
    for (int i = count - 1; i >= 0; --i) {
        if (entries[i].key && ascii::EqualsNoCase(entries[i].key, key)) return &entries[i];
    }
    return nullptr;
}

} // namespace

float* GetNumber(const RawEntry* entries, int count, const char* key)
{
    const RawEntry* entry = FindLast(entries, count, key);
    return entry && entry->type == Number ? entry->number : nullptr;
}

char* GetString(const RawEntry* entries, int count, const char* key)
{
    const RawEntry* entry = FindLast(entries, count, key);
    return entry && entry->type == String ? entry->string : nullptr;
}

bool Equal(const Entry& a, const Entry& b, std::string* diff)
{
    auto fail = [&](const char* field) {
        if (diff) *diff = field;
        return false;
    };
    if (a.key != b.key) return fail("key");
    if (a.type != b.type) return fail("type");
    if (a.hasString != b.hasString || a.string != b.string) return fail("string");
    if (a.hasNumber != b.hasNumber || (a.hasNumber && std::memcmp(&a.number, &b.number, sizeof(float)) != 0)) return fail("number");
    return true;
}

} // namespace obl::pis
