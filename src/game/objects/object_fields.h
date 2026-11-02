#pragma once

// The field tables of the C++ object classes (docs/architecture/objects.md). Each class has one
// `VisitFields(object, visitor)` in its Save order; every entry names its obl.save key (the world snapshot's record name too; the
// converter maps the old save labels onto these keys, importer/src/readers/save/save_writer.cpp, checked by the class's unit test
// through OldObjectFieldKeys) and whether the world snapshot hashes it. The record kind is the visitor method (Int, UInt, Float,
// Bool), whatever the C++ type of the field.
#include <concepts>
#include <type_traits>

namespace obl::object {

// Whether the world snapshot (schema 2, pinned by the reference hashes) holds the record.
enum class Hashed : bool { No, Yes };

// `Self` of a class's VisitFields: the class or the const class, so each class's overload only takes its own objects.
template<class Self, class Class>
concept SelfOf = std::same_as<std::remove_const_t<Self>, Class>;

} // namespace obl::object
