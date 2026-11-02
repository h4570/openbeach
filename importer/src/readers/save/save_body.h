#pragma once

#include "importer/src/readers/save/save_stream.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {

struct OldField {
    std::string name;
    SaveRecord record;
};

// A manager section or one object, in file order. The EventManager and Weather tails have no class record in the file;
// they are blocks of their own with the offset of their first record.
struct OldBlock {
    std::string kind; // section class ("World", "RepairManager") or object class ("GuestObject")
    size_t offset = 0;
    uint8_t type = 0; // object type byte (1..20) as written; 0 for sections
    std::vector<OldField> fields;
    // Objects: the first field of the shared Prop or Mover layers (then Entity), which every class
    // writes after its own fields.
    size_t sharedFrom = 0;

    // The first field with this label, or null.
    const OldField* Find(std::string_view name) const;
};

struct OldSaveBody {
    OldSaveHeader header;
    // Bytes of records that do not borrow an input buffer (a body built from an obl.save); shared by copies.
    std::shared_ptr<std::deque<std::vector<uint8_t>>> storage;
    std::vector<OldBlock> sections; // World, WorldMap, Path, ObjectManager (its own fields), Player, ... Weather
    std::vector<OldBlock> objects; // buildings, guests, then the others, in the order the object manager saves them
};

// Reads the header and every body record into typed blocks. Fails with an offset-qualified error on a record whose tag,
// array length or count does not fit the emitter, on a missing section and on records left over after the last one.
bool ReadOldSaveBody(std::span<const uint8_t> bytes, OldSaveBody& body, std::string& error);

// Where an emitter walk gets its records: the old record stream, or an obl.save mapped back (save_writer.h). `Take`
// appends the record for label `name` (tag, and the item count unless 0) to `block`; `Has` answers whether the optional
// or repeated part starting with record `label` follows (the stream checks the next records' `tags`). Failures set the
// source's error.
class RecordSource {
public:
    virtual ~RecordSource() = default;
    virtual bool Take(OldBlock& block, std::string name, SaveTag tag, uint32_t count, SaveRecord* out) = 0;
    virtual bool Has(const OldBlock& block, const std::string& label, std::span<const SaveTag> tags) = 0;
    virtual bool Class(OldBlock& block, std::string_view name) = 0;
    virtual bool Fail(size_t offset, const OldBlock& block, std::string_view field, std::string_view why) = 0;
    virtual size_t Offset() const = 0;
    virtual size_t Remaining() const = 0;
};

// The body walk over any source, after the header: every section and object in the emitters' order. `body.storage` is
// kept for the source's records.
bool ReadOldSaveBody(RecordSource& source, const OldSaveHeader& header, OldSaveBody& body);

// Consistency of a structurally read body: object counts against the ObjectManager header, unique ids, the id list and
// the next free id, references from objects and managers to object ids. Returns one line per problem; empty when clean.
std::vector<std::string> CheckOldSaveBody(const OldSaveBody& body);

// Text report of `obl-convert save-info --objects`: sections with their record counts, objects per class, the problems.
bool DescribeOldSaveObjects(std::span<const uint8_t> bytes, std::string& report, std::string& error);

} // namespace obl::convert
