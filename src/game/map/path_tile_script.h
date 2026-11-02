#pragma once

#include <string>

namespace obl::path_tile_script {

bool IsSpace(char c);

// The comment-skipping step of the path tile script: when text[0] is `/`, the number of bytes covering that comment line (up to and
// including its LF), the whitespace after it and every further comment line that follows; 0 otherwise. Any single `/` starts
// a comment (not only `//`), and the first byte is never tested for LF.
int SkipComments(const char* text);

// The token reader of the path tile script: skips whitespace and comments, then copies one token to `token` (a lone `;` or
// the bytes up to the next whitespace or `;`, which stays unread). Returns the bytes consumed from `text`, including the
// skipped whitespace and comments and the token itself.
int ReadToken(const char* text, std::string& token);

} // namespace obl::path_tile_script
