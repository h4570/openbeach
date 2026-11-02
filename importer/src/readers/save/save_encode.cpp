#include "importer/src/readers/save/save_encode.h"

#include <string>

namespace obl::convert {
namespace {

bool Counted(SaveTag tag)
{
    return tag == SaveTag::Class || tag == SaveTag::String || tag == SaveTag::WideString || tag == SaveTag::IntArray ||
            tag == SaveTag::ByteArray || tag == SaveTag::FloatArray;
}

class Encoder {
public:
    explicit Encoder(std::vector<uint8_t>& out) : m_out(out) {}

    void Record(const SaveRecord& r)
    {
        m_out.push_back(static_cast<uint8_t>(r.tag));
        if (Counted(r.tag)) U32(r.count);
        m_out.insert(m_out.end(), r.bytes.begin(), r.bytes.end());
    }
    void Class(const std::string& name)
    {
        m_out.push_back(static_cast<uint8_t>(SaveTag::Class));
        U32(static_cast<uint32_t>(name.size()));
        m_out.insert(m_out.end(), name.begin(), name.end());
    }
    void Scalar(SaveTag tag, uint32_t bits)
    {
        m_out.push_back(static_cast<uint8_t>(tag));
        const size_t width = tag == SaveTag::Bool ? 1 : 4;
        for (size_t i = 0; i < width; ++i) m_out.push_back(static_cast<uint8_t>(bits >> (8 * i)));
    }
    void Block(const OldBlock& block)
    {
        for (const OldField& f : block.fields) Record(f.record);
    }

private:
    void U32(uint32_t value)
    {
        for (int i = 0; i < 4; ++i) m_out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
    std::vector<uint8_t>& m_out;
};

} // namespace

std::vector<uint8_t> EncodeOldSave(const OldSaveBody& body)
{
    std::vector<uint8_t> bytes;
    Encoder e(bytes);
    const OldSaveHeader& h = body.header;
    e.Class("Binary Serial Stream");
    e.Class("BeachLife Save");
    e.Scalar(SaveTag::Float, 0x3fcccccd); // 1.6, the only version the reader accepts
    e.Scalar(SaveTag::Int, h.savedAt);
    e.Scalar(SaveTag::Int, static_cast<uint32_t>(h.scenario));
    e.Scalar(SaveTag::Int, static_cast<uint32_t>(h.day));
    e.Scalar(SaveTag::Float, h.dayTimeBits);
    e.Scalar(SaveTag::Float, h.cashBits);
    e.Scalar(SaveTag::Int, static_cast<uint32_t>(h.guests));
    SaveRecord title = h.title;
    title.tag = SaveTag::String;
    title.count = static_cast<uint32_t>(title.bytes.size());
    e.Record(title);
    e.Scalar(SaveTag::Bool, h.sandbox ? 1 : 0);
    for (const OldBlock& section : body.sections) {
        if (section.kind != "EventManager" && section.kind != "Weather") e.Class("Class name : " + section.kind);
        e.Block(section);
        if (section.kind == "ObjectManager")
            for (const OldBlock& object : body.objects) e.Block(object);
    }
    return bytes;
}

} // namespace obl::convert
