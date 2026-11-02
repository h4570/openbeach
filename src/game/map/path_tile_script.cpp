#include "game/map/path_tile_script.h"

namespace obl::path_tile_script {

bool IsSpace(char c)
{
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

int SkipComments(const char* text)
{
    if (text[0] != '/') return 0;
    int at = 0;
    do {
        ++at; // text[at] is never tested for LF when it is the `/` itself
        while (text[at] != '\n') ++at;
        while (IsSpace(text[at])) ++at;
    } while (text[at] == '/');
    return at;
}

int ReadToken(const char* text, std::string& token)
{
    token.clear();
    int at = 0;
    while (IsSpace(text[at])) ++at;
    at += SkipComments(text + at);
    if (text[at] == ';') {
        token = ";";
        return at + 1;
    }
    for (char c = text[at]; !IsSpace(c);) {
        token.push_back(c);
        ++at;
        c = text[at];
        if (c == ';') break;
    }
    return at;
}

} // namespace obl::path_tile_script
