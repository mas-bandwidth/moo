// This file is part of moo, a fork of the Luau programming language; moo changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4 math.noise (docs/DESIGN.md "math library"): this test-only float implementation is
// Luau's upstream reference and never runs inside the VM.
#include "lua.h"
#include "lualib.h"
#include "moo_number.h"
#include "MooHost.h"
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

extern fixed_t moo_math_noise(fixed_t x, fixed_t y, fixed_t z);

namespace
{
const unsigned char kFloatPerlinHash[257] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240, 21,  10,  23,
    190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203, 117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,
    125, 136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,
    41,  55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130,
    116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207,
    206, 59,  227, 47,  16,  58,  17,  182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,
    172, 9,   129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,  242, 193, 238, 210, 144,
    12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,
    127, 4,   150, 254, 138, 236, 205, 93,  222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180, 151
};

const float kFloatPerlinGrad[16][3] = {
    {1, 1, 0},
    {-1, 1, 0},
    {1, -1, 0},
    {-1, -1, 0},
    {1, 0, 1},
    {-1, 0, 1},
    {1, 0, -1},
    {-1, 0, -1},
    {0, 1, 1},
    {0, -1, 1},
    {0, 1, -1},
    {0, -1, -1},
    {1, 1, 0},
    {0, -1, 1},
    {-1, 1, 0},
    {0, -1, -1}
};


float floatFade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float floatLerp(float t, float a, float b) { return a + t * (b - a); }
float floatGrad(int h, float x, float y, float z)
{
    const float* g = kFloatPerlinGrad[h & 15];
    return g[0] * x + g[1] * y + g[2] * z;
}
float floatNoise(float x, float y, float z)
{
    float xflr = floorf(x), yflr = floorf(y), zflr = floorf(z);
    int xi = int(xflr) & 255, yi = int(yflr) & 255, zi = int(zflr) & 255;
    float xf = x - xflr, yf = y - yflr, zf = z - zflr;
    float u = floatFade(xf), v = floatFade(yf), w = floatFade(zf);
    const unsigned char* p = kFloatPerlinHash;
    int a = (p[xi] + yi) & 255, aa = (p[a] + zi) & 255, ab = (p[a + 1] + zi) & 255;
    int b = (p[xi + 1] + yi) & 255, ba = (p[b] + zi) & 255, bb = (p[b + 1] + zi) & 255;
    float la = floatLerp(u, floatGrad(p[aa], xf, yf, zf), floatGrad(p[ba], xf - 1, yf, zf));
    float lb = floatLerp(u, floatGrad(p[ab], xf, yf - 1, zf), floatGrad(p[bb], xf - 1, yf - 1, zf));
    float la1 = floatLerp(u, floatGrad(p[aa + 1], xf, yf, zf - 1), floatGrad(p[ba + 1], xf - 1, yf, zf - 1));
    float lb1 = floatLerp(u, floatGrad(p[ab + 1], xf, yf - 1, zf - 1), floatGrad(p[bb + 1], xf - 1, yf - 1, zf - 1));
    return floatLerp(w, floatLerp(v, la, lb), floatLerp(v, la1, lb1));
}
struct Lcg
{
    uint64_t s = 0x6d6f6f5f6c345f6eull;
    uint64_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return s ^ (s >> 29); }
    int64_t nextRaw() { return int64_t(next() % (uint64_t(512) * 65536)) - int64_t(256 * 65536); }
};
uint64_t hashRaw(uint64_t h, int64_t raw)
{
    uint64_t u = uint64_t(raw);
    for (int i = 0; i < 8; ++i) { h ^= uint8_t(u >> (8 * i)); h *= 1099511628211ull; }
    return h;
}
std::array<int64_t, 3> sample(Lcg& r) { return {r.nextRaw(), r.nextRaw(), r.nextRaw()}; }
} // namespace

TEST_SUITE_BEGIN("moo-l4-math-noise");

TEST_CASE("MathNoiseFrozen64AndLatticeZeros")
{
    Lcg rng;
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 64; ++i)
    {
        auto p = sample(rng);
        int64_t out = moo_math_noise(p[0], p[1], p[2]);
        CHECK(out >= -65536); CHECK(out <= 65536);
        hash = hashRaw(hash, out);
    }
    INFO("frozen raw hash: 0x" << std::hex << hash);
    CHECK(hash == 12883597645287845634ull); // FNV-1a of 64 reviewed raw outputs
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            for (int z = -2; z <= 2; ++z)
                CHECK(moo_math_noise(int64_t(x) * 65536, int64_t(y) * 65536, int64_t(z) * 65536) == 0);
}

TEST_CASE("MathNoiseMaximumDifferenceFromLuauFloat")
{
    Lcg rng;
    double maximum = 0.0;
    for (int i = 0; i < 65536; ++i)
    {
        auto p = sample(rng);
        float x = float(double(p[0]) / 65536.0), y = float(double(p[1]) / 65536.0), z = float(double(p[2]) / 65536.0);
        double fixed = double(moo_math_noise(p[0], p[1], p[2])) / 65536.0;
        maximum = std::max(maximum, std::abs(fixed - double(floatNoise(x, y, z))));
    }
    std::cout << "MathNoiseMaximumDifferenceFromLuauFloat=" << maximum << "\n";
    CHECK(maximum < 0.001);
}

TEST_CASE("MathNoiseLibraryFastcallBitExact")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    Lcg rng;
    for (int i = 0; i < 64; ++i)
    {
        auto p = sample(rng);
        std::string arguments = "moo_test.fromraw('" + std::to_string(p[0]) + "'), moo_test.fromraw('" + std::to_string(p[1]) +
            "'), moo_test.fromraw('" + std::to_string(p[2]) + "')";
        std::string direct = "return moo_test.raw(math.noise(" + arguments + "))";
        std::string library = "local f = math.noise; return moo_test.raw(f(" + arguments + "))";
        int64_t expected = moo_math_noise(p[0], p[1], p[2]);
        CHECK(std::stoll(mooRun(L, direct.c_str())) == expected);
        CHECK(std::stoll(mooRun(L, library.c_str())) == expected);
    }

    lua_close(L);
}

TEST_SUITE_END();
