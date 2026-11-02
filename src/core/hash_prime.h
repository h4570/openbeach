#pragma once

#include <cstdint>

namespace obl::hash_prime {

// The game's bucket-count table (28 words): primes growing by about a factor of two, ending with the largest prime below 2^32.
inline constexpr uint32_t kPrimes[] = {53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317, 196613, 393241, 786433, 1572869,
        3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189, 805306457, 1610612741, 3221225473u, 4294967291u};

// The first table entry that is not below `minimum`. A request above the largest entry comes back unchanged
// (the argument is returned unchanged when the scan runs out).
constexpr uint32_t NextPrime(uint32_t minimum)
{
    for (uint32_t prime : kPrimes)
        if (prime >= minimum) return prime;
    return minimum;
}

} // namespace obl::hash_prime
