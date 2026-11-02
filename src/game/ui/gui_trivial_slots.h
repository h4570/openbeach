#pragma once

// The trivial methods and the objects' CallFunction default that the GUI
// widgets and the world step reuse.
namespace obl::gui {
void EmptyMethod(void* target);
void EmptyDestructor(void* target);
void EmptyCallFunction(void* target, int id);
} // namespace obl::gui
