#include "game/mods/ed25519.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace obl::ed25519 {

namespace {

// ---- SHA-512 (FIPS 180-4) ----

constexpr std::array<uint64_t, 80> kSha512K = {
        0x428a2f98d728ae22ULL,
        0x7137449123ef65cdULL,
        0xb5c0fbcfec4d3b2fULL,
        0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL,
        0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL,
        0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL,
        0x12835b0145706fbeULL,
        0x243185be4ee4b28cULL,
        0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL,
        0x80deb1fe3b1696b1ULL,
        0x9bdc06a725c71235ULL,
        0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL,
        0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL,
        0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL,
        0x4a7484aa6ea6e483ULL,
        0x5cb0a9dcbd41fbd4ULL,
        0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL,
        0xa831c66d2db43210ULL,
        0xb00327c898fb213fULL,
        0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL,
        0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL,
        0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL,
        0x2e1b21385c26c926ULL,
        0x4d2c6dfc5ac42aedULL,
        0x53380d139d95b3dfULL,
        0x650a73548baf63deULL,
        0x766a0abb3c77b2a8ULL,
        0x81c2c92e47edaee6ULL,
        0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL,
        0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL,
        0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL,
        0xd69906245565a910ULL,
        0xf40e35855771202aULL,
        0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL,
        0x1e376c085141ab53ULL,
        0x2748774cdf8eeb99ULL,
        0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL,
        0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL,
        0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL,
        0x78a5636f43172f60ULL,
        0x84c87814a1f0ab72ULL,
        0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL,
        0xa4506cebde82bde9ULL,
        0xbef9a3f7b2c67915ULL,
        0xc67178f2e372532bULL,
        0xca273eceea26619cULL,
        0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL,
        0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL,
        0x0a637dc5a2c898a6ULL,
        0x113f9804bef90daeULL,
        0x1b710b35131c471bULL,
        0x28db77f523047d84ULL,
        0x32caab7b40c72493ULL,
        0x3c9ebe0a15c9bebcULL,
        0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL,
        0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL,
        0x6c44198c4a475817ULL,
};

uint64_t Rotr64(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

class Sha512 {
public:
    void Update(std::span<const uint8_t> data)
    {
        for (uint8_t byte : data) {
            m_block[m_used++] = byte;
            if (m_used == 128) {
                Compress(m_block.data());
                m_used = 0;
            }
        }
        m_total += data.size();
    }

    std::array<uint8_t, 64> Final()
    {
        const uint64_t bitsHi = m_total >> 61;
        const uint64_t bitsLo = m_total << 3;
        const uint8_t pad = 0x80;
        Update({&pad, 1});
        const uint8_t zero = 0;
        while (m_used != 112) Update({&zero, 1});
        uint8_t len[16];
        for (int i = 0; i < 8; ++i) {
            len[i] = static_cast<uint8_t>(bitsHi >> (56 - 8 * i));
            len[8 + i] = static_cast<uint8_t>(bitsLo >> (56 - 8 * i));
        }
        Update(len);
        std::array<uint8_t, 64> out{};
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 8; ++j) out[static_cast<size_t>(i * 8 + j)] = static_cast<uint8_t>(m_h[i] >> (56 - 8 * j));
        }
        return out;
    }

private:
    void Compress(const uint8_t* block)
    {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 8; ++j) w[i] = (w[i] << 8) | block[i * 8 + j];
        }
        for (int i = 16; i < 80; ++i) {
            const uint64_t s0 = Rotr64(w[i - 15], 1) ^ Rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = Rotr64(w[i - 2], 19) ^ Rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
        uint64_t e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
        for (int i = 0; i < 80; ++i) {
            const uint64_t s1 = Rotr64(e, 14) ^ Rotr64(e, 18) ^ Rotr64(e, 41);
            const uint64_t ch = (e & f) ^ ((~e) & g);
            const uint64_t t1 = h + s1 + ch + kSha512K[static_cast<size_t>(i)] + w[i];
            const uint64_t s0 = Rotr64(a, 28) ^ Rotr64(a, 34) ^ Rotr64(a, 39);
            const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint64_t t2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        m_h[0] += a;
        m_h[1] += b;
        m_h[2] += c;
        m_h[3] += d;
        m_h[4] += e;
        m_h[5] += f;
        m_h[6] += g;
        m_h[7] += h;
    }

    uint64_t m_h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL,
            0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    std::array<uint8_t, 128> m_block{};
    size_t m_used = 0;
    uint64_t m_total = 0;
};

// ---- GF(2^255 - 19) using 16 signed 64-bit limbs (16 bits per limb) ----

using Gf = std::array<int64_t, 16>;

constexpr Gf kGf0 = {0};
constexpr Gf kGf1 = {1};
// d = -121665/121666 mod p
constexpr Gf kD = {
        0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
// 2 * d mod p
constexpr Gf kD2 = {
        0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
// Base point B = (X, Y)
constexpr Gf kX = {
        0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
constexpr Gf kY = {
        0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
// sqrt(-1) mod p
constexpr Gf kI = {
        0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};

// Group order L (little-endian bytes)
constexpr uint8_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

void Carry(Gf& o)
{
    for (int i = 0; i < 16; ++i) {
        o[static_cast<size_t>(i)] += 1LL << 16;
        const int64_t c = o[static_cast<size_t>(i)] >> 16;
        o[static_cast<size_t>((i + 1) * (i < 15))] += c - 1 + 37 * (c - 1) * (i == 15);
        o[static_cast<size_t>(i)] -= c << 16;
    }
}

void Select(Gf& p, Gf& q, int b)
{
    const int64_t c = ~(b - 1);
    for (size_t i = 0; i < 16; ++i) {
        const int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void Pack25519(uint8_t o[32], const Gf& n)
{
    Gf t = n;
    Carry(t);
    Carry(t);
    Carry(t);
    Gf m{};
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[static_cast<size_t>(i)] = t[static_cast<size_t>(i)] - 0xffff - ((m[static_cast<size_t>(i - 1)] >> 16) & 1);
            m[static_cast<size_t>(i - 1)] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = static_cast<int>((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        Select(t, m, 1 - b);
    }
    for (size_t i = 0; i < 16; ++i) {
        o[2 * i] = static_cast<uint8_t>(t[i] & 0xff);
        o[2 * i + 1] = static_cast<uint8_t>(t[i] >> 8);
    }
}

bool Neq25519(const Gf& a, const Gf& b)
{
    uint8_t c[32], d[32];
    Pack25519(c, a);
    Pack25519(d, b);
    uint32_t diff = 0;
    for (int i = 0; i < 32; ++i) diff |= c[i] ^ d[i];
    return diff != 0;
}

uint8_t Par25519(const Gf& a)
{
    uint8_t d[32];
    Pack25519(d, a);
    return d[0] & 1;
}

void Unpack25519(Gf& o, const uint8_t n[32])
{
    for (size_t i = 0; i < 16; ++i) o[i] = n[2 * i] + (static_cast<int64_t>(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

Gf Add(const Gf& a, const Gf& b)
{
    Gf o;
    for (size_t i = 0; i < 16; ++i) o[i] = a[i] + b[i];
    return o;
}

Gf Sub(const Gf& a, const Gf& b)
{
    Gf o;
    for (size_t i = 0; i < 16; ++i) o[i] = a[i] - b[i];
    return o;
}

Gf Mul(const Gf& a, const Gf& b)
{
    int64_t t[31] = {};
    for (size_t i = 0; i < 16; ++i) {
        for (size_t j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    }
    for (size_t i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    Gf o;
    for (size_t i = 0; i < 16; ++i) o[i] = t[i];
    Carry(o);
    Carry(o);
    return o;
}

Gf Sq(const Gf& a)
{
    return Mul(a, a);
}

Gf Inv25519(const Gf& i)
{
    Gf c = i;
    for (int a = 253; a >= 0; --a) {
        c = Sq(c);
        if (a != 2 && a != 4) c = Mul(c, i);
    }
    return c;
}

Gf Pow2523(const Gf& i)
{
    Gf c = i;
    for (int a = 250; a >= 0; --a) {
        c = Sq(c);
        if (a != 1) c = Mul(c, i);
    }
    return c;
}

// Extended twisted Edwards point (X, Y, Z, T)
using Point = std::array<Gf, 4>;

void AddPoints(Point& p, const Point& q)
{
    const Gf a = Mul(Sub(p[1], p[0]), Sub(q[1], q[0]));
    const Gf b = Mul(Add(p[0], p[1]), Add(q[0], q[1]));
    const Gf c = Mul(Mul(p[3], q[3]), kD2);
    Gf d = Mul(p[2], q[2]);
    d = Add(d, d);
    const Gf e = Sub(b, a);
    const Gf f = Sub(d, c);
    const Gf g = Add(d, c);
    const Gf h = Add(b, a);
    p[0] = Mul(e, f);
    p[1] = Mul(h, g);
    p[2] = Mul(g, f);
    p[3] = Mul(e, h);
}

void Cswap(Point& p, Point& q, int b)
{
    for (size_t i = 0; i < 4; ++i) Select(p[i], q[i], b);
}

void ScalarMult(Point& p, Point q, const uint8_t s[32])
{
    p = {kGf0, kGf1, kGf1, kGf0};
    for (int i = 255; i >= 0; --i) {
        const int b = (s[i / 8] >> (i & 7)) & 1;
        Cswap(p, q, b);
        AddPoints(q, p);
        AddPoints(p, p);
        Cswap(p, q, b);
    }
}

void ScalarBase(Point& p, const uint8_t s[32])
{
    Point q = {kX, kY, kGf1, Mul(kX, kY)};
    ScalarMult(p, q, s);
}

void PackPoint(uint8_t r[32], const Point& p)
{
    const Gf zi = Inv25519(p[2]);
    const Gf tx = Mul(p[0], zi);
    const Gf ty = Mul(p[1], zi);
    Pack25519(r, ty);
    r[31] ^= static_cast<uint8_t>(Par25519(tx) << 7);
}

bool UnpackNeg(Point& r, const uint8_t p[32])
{
    // Reject non-canonical y >= 2^255 - 19 (RFC 8032 strictness).
    uint8_t yBytes[32];
    std::memcpy(yBytes, p, 32);
    yBytes[31] &= 0x7f;
    static constexpr uint8_t kP[32] = {0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};
    bool less = false;
    for (int i = 31; i >= 0; --i) {
        if (yBytes[i] < kP[i]) {
            less = true;
            break;
        }
        if (yBytes[i] > kP[i]) return false;
    }
    if (!less) return false;

    r[2] = kGf1;
    Unpack25519(r[1], p);
    const Gf num = Sub(Sq(r[1]), kGf1);
    const Gf den = Add(Mul(Sq(r[1]), kD), kGf1);
    const Gf den2 = Sq(den);
    const Gf den4 = Sq(den2);
    Gf chk = Mul(Mul(Mul(den4, den2), den), num);
    r[0] = Mul(Pow2523(chk), Mul(Mul(num, den2), den));

    chk = Mul(Sq(r[0]), den);
    if (Neq25519(chk, num)) r[0] = Mul(r[0], kI);
    chk = Mul(Sq(r[0]), den);
    if (Neq25519(chk, num)) return false;

    // RFC 8032: if x == 0 and sign bit is 1, decoding fails.
    if (!Neq25519(r[0], kGf0) && (p[31] >> 7)) return false;

    if (Par25519(r[0]) == (p[31] >> 7)) r[0] = Sub(kGf0, r[0]);
    r[3] = Mul(r[0], r[1]);
    return true;
}

void ModL(uint8_t r[32], int64_t x[64])
{
    for (int i = 63; i >= 32; --i) {
        int64_t carry = 0;
        for (int j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * kL[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[i - 12] += carry;
        x[i] = 0;
    }
    int64_t carry = 0;
    for (int j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * kL[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; ++j) x[j] -= carry * kL[j];
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = static_cast<uint8_t>(x[i] & 255);
    }
}

void Reduce(uint8_t r[64])
{
    int64_t x[64];
    for (int i = 0; i < 64; ++i) x[i] = r[i];
    ModL(r, x);
}

bool ScalarLessThanL(const uint8_t s[32])
{
    for (int i = 31; i >= 0; --i) {
        if (s[i] < kL[i]) return true;
        if (s[i] > kL[i]) return false;
    }
    return false;
}

} // namespace

PublicKey PublicKeyFromSeed(const Seed& seed)
{
    Sha512 hasher;
    hasher.Update(seed);
    auto d = hasher.Final();
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;

    Point p;
    ScalarBase(p, d.data());
    PublicKey pk{};
    PackPoint(pk.data(), p);
    return pk;
}

Signature Sign(const Seed& seed, std::span<const uint8_t> message)
{
    Sha512 h1;
    h1.Update(seed);
    auto d = h1.Final();
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;

    Point p;
    ScalarBase(p, d.data());
    PublicKey pk{};
    PackPoint(pk.data(), p);

    Sha512 h2;
    h2.Update({d.data() + 32, 32});
    h2.Update(message);
    auto r = h2.Final();
    Reduce(r.data());

    Point rp;
    ScalarBase(rp, r.data());
    Signature sig{};
    PackPoint(sig.data(), rp);

    Sha512 h3;
    h3.Update({sig.data(), 32});
    h3.Update(pk);
    h3.Update(message);
    auto h = h3.Final();
    Reduce(h.data());

    int64_t x[64] = {};
    for (int i = 0; i < 32; ++i) x[i] = r[i];
    for (int i = 0; i < 32; ++i) {
        for (int j = 0; j < 32; ++j) x[i + j] += static_cast<int64_t>(h[i]) * d[j];
    }
    ModL(sig.data() + 32, x);
    return sig;
}

bool Verify(const PublicKey& publicKey, std::span<const uint8_t> message, const Signature& signature)
{
    // RFC 8032 requires S < L to prevent signature malleability.
    if (!ScalarLessThanL(signature.data() + 32)) return false;

    Point p;
    if (!UnpackNeg(p, publicKey.data())) return false;

    Sha512 h;
    h.Update({signature.data(), 32});
    h.Update(publicKey);
    h.Update(message);
    auto digest = h.Final();
    Reduce(digest.data());

    Point q;
    ScalarMult(q, p, digest.data());
    Point sb;
    ScalarBase(sb, signature.data() + 32);
    AddPoints(q, sb);

    uint8_t check[32];
    PackPoint(check, q);
    uint32_t diff = 0;
    for (int i = 0; i < 32; ++i) diff |= check[i] ^ signature[static_cast<size_t>(i)];
    return diff == 0;
}

} // namespace obl::ed25519
