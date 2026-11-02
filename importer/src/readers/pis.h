#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Text parser behind the `.pis` configuration files. Pure logic with no game memory, so it is unit-tested directly.
//
// A file is a list of `'KEY' value` pairs; value = number, `"string"` or garbage (type 0). `//` comments run to
// the next CR. The scanners work on `(text, length)` windows that are not NUL-terminated by themselves and compare
// signed `char`s (bytes >= 0x80 behave like control characters).
namespace obl::pis {

// Value kinds stored in an entry.
enum ValueType : int32_t { None = 0, String = 1, Number = 2 };

// Index after the second `"` in the window, 0 when there is none.
int QuotedLength(const char* text, int length);
// index after the closing `'` of a key that starts at text[0] (`length` when it is missing, 1 for length <= 1).
int FindKeyEnd(const char* text, int length);
// length of the value token: `"..."` including both quotes, otherwise up to the first byte outside '!'..'y'.
int TokenLength(const char* text, int length);
// number of leading bytes outside '!'..'y' (whitespace, junk, >= 0x80).
int SkipSeparators(const char* text, int length);
// at least two `'` in the window, so another key can follow.
bool HasKey(const char* text, int length);
// offset of the next `'`, skipping from any `/` that follows a byte to the next CR (comments).
int SkipToKey(const char* text, int length);
// decimal number, optional leading `-`, optional fraction. Reads until the first non-digit (the buffer must
// be NUL-terminated). The computation runs in x87 at 53-bit precision with a float 0.1f scale; double reproduces it.
double ParseNumber(const char* text);
// Number when text[0] is a digit, '.' or '-'; String with two `"` in the window; otherwise None.
int ClassifyValue(const char* text, int length);

// Number of entries `ParseEntries` produces for the same buffer.
int CountEntries(const char* text, int length);

// What `KeyText` and `ValueEntry` produce for one entry.
struct Entry {
    std::string key;
    int32_t type = None;
    bool hasString = false; // the game leaves +8 null for an unterminated string
    std::string string;
    bool hasNumber = false;
    float number = 0.0f;
};

// Key text between the quotes. `text[0]` is the opening quote.
std::string KeyText(const char* text, int length);
// Type and payload of the value token (`length` = TokenLength result).
Entry ValueEntry(const char* text, int length);

// Every entry in file order; its size equals CountEntries.
std::vector<Entry> ParseEntries(const char* text, int length);

// One record of the game's in-memory entry array (16 bytes each).
struct RawEntry {
    char* key;
    int32_t type;
    char* string;
    float* number;
};

// The LAST entry whose key matches decides; when it has the
// wrong type the result is null (earlier duplicates are not consulted). The number comes back as a pointer to the stored float.
float* GetNumber(const RawEntry* entries, int count, const char* key);
char* GetString(const RawEntry* entries, int count, const char* key);

// True when both entries hold the same type/key/payload; `diff` (optional) receives the first mismatching field.
bool Equal(const Entry& a, const Entry& b, std::string* diff = nullptr);

} // namespace obl::pis
