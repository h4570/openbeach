#pragma once

#include "importer/src/readers/exe_tables.h"
#include "synthetic_exe.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace obl::test {

using convert::kFootprintBlockSize;
using convert::kFootprintCount;

inline constexpr uint32_t kFootprintTextRva = 0x1000;
inline constexpr uint32_t kFootprintTextRaw = SyntheticExe::kHeaderSize; // .text is the first section
inline constexpr uint32_t kFootprintTextSize = 0x4000;
inline constexpr char kFootprintMessage[] = "building key not found"; // the lookup error path pushes a message; no anchor
inline constexpr size_t kFootprintInlined = 21; // keys looked up inline before the code switches to calls (as in every build)
// Cells inside every grid's area (row 1 column 1 of layer 0, row 0 column 0 of layer 1), marked per grid.
inline constexpr uint32_t kFootprintMarkedCells[] = {26, 625};

// The size of grid `i`, in cells.
inline uint32_t FootprintWidth(size_t i)
{
    return static_cast<uint32_t>(1 + i % 20);
}
inline uint32_t FootprintHeight(size_t i)
{
    return static_cast<uint32_t>(2 + i % 5);
}

inline std::string KeyName(size_t i)
{
    char name[16];
    std::snprintf(name, sizeof(name), "KEY%02zu", i);
    return name;
}

// A PE32 image with a .text section holding the assignment code and a .data section holding the message, the keys and
// the grids. Vary `shift` to test data relocation and the resulting address changes between supported copies.
struct FootprintExe : SyntheticExe {
    uint32_t dataRva = 0;
    size_t dataRaw = 0;
    std::vector<uint32_t> keyVa, gridVa;
    uint32_t messageVa = 0;
    std::vector<size_t> storeOffsets; // file offsets of the grid stores, in code order
    std::vector<size_t> keyPushOffsets; // file offsets of `push <key>` per key

    // Set `codeFlags` to 0xC0000040 to model the UK/US CD, whose .text section is flagged as read/write data.
    explicit FootprintExe(uint32_t shift = 0, uint32_t codeFlags = kCodeFlags)
    {
        dataRva = 0x10000 + shift;
        const uint32_t stringsSize = 0x400;
        const uint32_t dataSize = stringsSize + static_cast<uint32_t>(kFootprintCount) * kFootprintBlockSize;
        AddSection(".text", kFootprintTextRva, kFootprintTextSize, codeFlags);
        dataRaw = AddSection(".data", dataRva, dataSize, kDataFlags);

        // .data: the message, the keys, then the grids in a different order than the code assigns them.
        size_t at = dataRaw;
        messageVa = kImageBase + dataRva;
        std::memcpy(&bytes[at], kFootprintMessage, sizeof(kFootprintMessage));
        at += 0x40;
        for (size_t i = 0; i < kFootprintCount; ++i) {
            const std::string key = KeyName(i);
            keyVa.push_back(kImageBase + dataRva + static_cast<uint32_t>(at - dataRaw));
            std::memcpy(&bytes[at], key.c_str(), key.size() + 1);
            at += 16;
        }
        for (size_t i = 0; i < kFootprintCount; ++i) {
            const uint32_t slot = static_cast<uint32_t>((i * 7) % kFootprintCount);
            const uint32_t offset = stringsSize + slot * kFootprintBlockSize;
            gridVa.push_back(kImageBase + dataRva + offset);
            const size_t grid = dataRaw + offset;
            PutU32(bytes, grid, FootprintWidth(i));
            PutU32(bytes, grid + 4, FootprintHeight(i));
            for (uint32_t c : kFootprintMarkedCells) PutU32(bytes, grid + 8 + c * 4, static_cast<uint32_t>(i * 1000 + c));
        }

        EmitCode();
    }

    size_t pc = kFootprintTextRaw;
    std::vector<std::pair<size_t, size_t>> calls; // (file offset of the rel32, target file offset)

    void Op(std::initializer_list<uint8_t> op)
    {
        for (uint8_t b : op) bytes[pc++] = b;
    }
    void Imm(uint32_t v)
    {
        PutU32(bytes, pc, v);
        pc += 4;
    }
    void Push(uint32_t v)
    {
        Op({0x68});
        Imm(v);
    }
    void Call(size_t target)
    {
        Op({0xE8});
        calls.push_back({pc, target});
        pc += 4;
    }
    void Store(size_t key)
    {
        storeOffsets.push_back(pc);
        Op({0xC7, 0x84, 0x86, 0x30, 0x42, 0x00, 0x00}); // mov [esi + eax*4 + 0x4230], grid
        Imm(gridVa[key]);
    }
    void PushKey(size_t key)
    {
        keyPushOffsets.push_back(pc);
        Push(keyVa[key]);
    }

    void EmitCode()
    {
        const size_t errorRoutine = kFootprintTextRaw + kFootprintTextSize - 0x20;
        const size_t lookupRoutine = kFootprintTextRaw + 0x10;
        // The out-of-line lookup pushes its key from a register: not a match for the inlined pattern.
        pc = lookupRoutine;
        Op({0x52}); // push edx
        Push(messageVa);
        Call(errorRoutine);
        Op({0x83, 0xC4, 0x08, 0xC3});

        pc = kFootprintTextRaw + 0x100;
        for (size_t i = 0; i < kFootprintInlined; ++i) {
            Op({0xBF}); // mov edi, key (the inlined strcmp)
            Imm(keyVa[i]);
            Op({0x8B, 0x56, 0x0C, 0x33, 0xED}); // mov edx, [esi + 0xc]; xor ebp, ebp
            PushKey(i); // error path: push key; push message; call error; add esp, 8; xor eax, eax
            Push(messageVa);
            Call(errorRoutine);
            Op({0x83, 0xC4, 0x08, 0x33, 0xC0, 0x8D, 0x0C, 0x40}); // ...; lea ecx, [eax + eax*2]
            if (i + 1 < kFootprintInlined) Store(i);
        }
        // From here the key is pushed before the previous key's store: push next; mov ecx, esi; store; call lookup.
        for (size_t i = kFootprintInlined - 1; i + 1 < kFootprintCount; ++i) {
            PushKey(i + 1);
            Op({0x8B, 0xCE});
            Store(i);
            Call(lookupRoutine);
            Op({0x8D, 0x0C, 0x40, 0x8D, 0x14, 0x80}); // lea ecx, [eax + eax*2]; lea edx, [eax + eax*4]
        }
        Store(kFootprintCount - 1);
        Op({0x5E, 0xC3}); // pop esi; ret

        pc = errorRoutine;
        Op({0xC3});
        for (auto [at, target] : calls) PutU32(bytes, at, static_cast<uint32_t>(target - (at + 4)));
    }

    // Byte offset in the file of a virtual address inside .data.
    size_t DataOffset(uint32_t va) const { return dataRaw + (va - kImageBase - dataRva); }
};

} // namespace obl::test
