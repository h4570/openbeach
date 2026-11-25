#pragma once

#include <array>
#include <cstdint>
#include <span>

// Portable Ed25519 (RFC 8032) signing and verification for mod catalog indexes.
// Uses 16-limb field arithmetic so it builds identically on MSVC, GCC, Clang and WebAssembly.
namespace obl::ed25519 {

inline constexpr size_t kSeedSize = 32;
inline constexpr size_t kPublicKeySize = 32;
inline constexpr size_t kSignatureSize = 64;

using Seed = std::array<uint8_t, kSeedSize>;
using PublicKey = std::array<uint8_t, kPublicKeySize>;
using Signature = std::array<uint8_t, kSignatureSize>;

PublicKey PublicKeyFromSeed(const Seed& seed);
Signature Sign(const Seed& seed, std::span<const uint8_t> message);
bool Verify(const PublicKey& publicKey, std::span<const uint8_t> message, const Signature& signature);

} // namespace obl::ed25519
