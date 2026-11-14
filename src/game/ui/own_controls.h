#pragma once

#include <string>

// Controls OpenBeach adds to the original game's dialogs. The converted layouts are the original scripts, byte for byte (the
// converter never edits them), so an own control is inserted into the script text after it is read, and its texts come from
// own_texts.h. Today: the pointer speed slider in the Video options, for builds whose pointer is a thumb stick.
namespace obl::own_controls {

// Whether this host moves the pointer with a stick (set once by the platform's input backend); the speed slider is then added.
void SetPadPointer(bool available);
bool PadPointer();

// The text of a layout file as the dialogs should parse it: `script` with the own controls inserted. Without a Video tab or without a
// stick it is `script` unchanged.
std::string Apply(std::string script);

} // namespace obl::own_controls
