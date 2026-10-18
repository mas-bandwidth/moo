// moo: TEMPORARY BRIDGE until the math library is rebuilt on fixed (docs/DESIGN.md, "math library"): the functions
// that call libm convert through doubles (moo_numbridgetodouble, moo_numbridgefromdouble). abs, ceil, floor, min, max,
// clamp, sign, round and random are exact on the number already.
// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lualib.h"

#include "lstate.h"
#include "moo.h"

#include "fixed/fixed_math.h"

#include <limits>
#include <math.h>

// moo: exact Q48.16 constants, each rounded once to nearest 2^-16 (docs/DESIGN.md "math library")
#define MOO_MATH_PI (fixed_t(205887))          // round(pi * 2^16): 3.14159265358979323846 * 65536 = 205887.416...
#define MOO_MATH_DEG_PER_RAD (fixed_t(3754936)) // round((180 / pi) * 2^16): 57.29577951308232... * 65536 = 3754936.206...
#define MOO_MATH_RAD_PER_DEG (fixed_t(1144))    // round((pi / 180) * 2^16): 0.017453292519943295... * 65536 = 1143.818...
#define MOO_MATH_E (fixed_t(178145))            // round(e * 2^16): 2.71828182845904523536 * 65536 = 178145.382...
#define MOO_MATH_PHI (fixed_t(106039))          // round(phi * 2^16): 1.61803398874989484820 * 65536 = 106039.275...
#define MOO_MATH_SQRT2 (fixed_t(92682))         // round(sqrt(2) * 2^16): 1.41421356237309504880 * 65536 = 92681.900...
#define MOO_MATH_TAU (fixed_t(411775))          // round(2 * pi * 2^16): 6.28318530717958647692 * 65536 = 411774.832...

#define PCG32_INC 105

LUAU_FASTFLAGVARIABLE(FixMathNoisePrecision)

static uint32_t pcg32_random(uint64_t* state)
{
    uint64_t oldstate = *state;
    *state = oldstate * 6364136223846793005ULL + (PCG32_INC | 1);
    uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = uint32_t(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-int32_t(rot)) & 31));
}

static void pcg32_seed(uint64_t* state, uint64_t seed)
{
    *state = 0;
    pcg32_random(state);
    *state += seed;
    pcg32_random(state);
}

static int math_abs(lua_State* L)
{
    lua_Number v = luaL_checknumber(L, 1);
    lua_pushnumber(L, v < 0 ? moo_numunm(v) : v);
    return 1;
}

// moo: sin, cos and tan on fixed (docs/DESIGN.md "math library"): one definition serves both the library
// functions here and their bytecode fastcalls in lbuiltins.cpp, so both give the same bits. tan returns
// false on a zero cosine so the library raises "division by zero" and the fastcall falls back to it.
fixed_t moo_math_sin(fixed_t radians)
{
    return fixSin(radians);
}

fixed_t moo_math_cos(fixed_t radians)
{
    return fixCos(radians);
}

bool moo_math_tan(fixed_t radians, fixed_t* out)
{
    fixCosSin cs = fixComputeCosSin(radians);
    if (cs.cosine == 0)
        return false;
    *out = moo_numdiv(cs.sine, cs.cosine);
    return true;
}

// moo: fmod, modf, frexp, ldexp, deg, rad and lerp on the raw Q48.16 value (docs/DESIGN.md "math library"):
// one shared definition serves both the library entry points here and their bytecode fastcalls in lbuiltins.cpp.
bool moo_math_fmod(fixed_t a, fixed_t b, fixed_t* out)
{
    if (b == 0)
        return false;
    *out = (b == -1) ? 0 : (a % b);
    return true;
}

void moo_math_modf(fixed_t a, fixed_t* ip, fixed_t* fp)
{
    uint64_t u = a < 0 ? uint64_t(0) - uint64_t(a) : uint64_t(a);
    uint64_t f = u & uint64_t(MOO_NUMBER_ONE - 1);
    uint64_t i = u - f;
    *ip = a < 0 ? -fixed_t(i) : fixed_t(i);
    *fp = a < 0 ? -fixed_t(f) : fixed_t(f);
}

void moo_math_frexp(fixed_t a, fixed_t* m, int* e)
{
    if (a == 0)
    {
        *m = 0;
        *e = 0;
        return;
    }

    uint64_t u = a < 0 ? uint64_t(0) - uint64_t(a) : uint64_t(a);
    int msb = 0;
    for (uint64_t x = u >> 1; x != 0; x >>= 1)
        msb += 1;

    uint64_t mag = msb <= 15 ? (u << (15 - msb)) : (u >> (msb - 15));
    *m = a < 0 ? -fixed_t(mag) : fixed_t(mag);
    *e = msb - 15;
}

fixed_t moo_math_ldexp(fixed_t a, int e)
{
    if (a == 0 || e == 0)
        return a;

    uint64_t u = a < 0 ? uint64_t(0) - uint64_t(a) : uint64_t(a);
    if (e > 0)
    {
        if (e >= 63 || u > (uint64_t(MOO_NUMBER_MAX) >> e))
            return a < 0 ? MOO_NUMBER_MIN : MOO_NUMBER_MAX;
        uint64_t r = u << e;
        return a < 0 ? -fixed_t(r) : fixed_t(r);
    }

    uint64_t s = uint64_t(0) - uint64_t(int64_t(e));
    if (s >= 63)
        return 0;
    uint64_t r = u >> s;
    return a < 0 ? -fixed_t(r) : fixed_t(r);
}

fixed_t moo_math_deg(fixed_t a)
{
    return fixMul(a, MOO_MATH_DEG_PER_RAD);
}

fixed_t moo_math_rad(fixed_t a)
{
    return fixMul(a, MOO_MATH_RAD_PER_DEG);
}

fixed_t moo_math_lerp(fixed_t a, fixed_t b, fixed_t t)
{
    return t == MOO_NUMBER_ONE ? b : moo_numadd(a, moo_nummul(moo_numsub(b, a), t));
}

static int math_sin(lua_State* L)
{
    lua_pushnumber(L, moo_math_sin(luaL_checknumber(L, 1)));
    return 1;
}

static int math_sinh(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(sinh(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

static int math_cos(lua_State* L)
{
    lua_pushnumber(L, moo_math_cos(luaL_checknumber(L, 1)));
    return 1;
}

static int math_cosh(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(cosh(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

static int math_tan(lua_State* L)
{
    fixed_t out;
    if (!moo_math_tan(luaL_checknumber(L, 1), &out))
        luaL_error(L, "division by zero");
    lua_pushnumber(L, out);
    return 1;
}

static int math_tanh(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(tanh(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

static int math_asin(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(asin(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

static int math_acos(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(acos(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

static int math_atan(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(atan(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

static int math_atan2(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(atan2(moo_numbridgetodouble(luaL_checknumber(L, 1)), moo_numbridgetodouble(luaL_checknumber(L, 2)))));
    return 1;
}

static int math_ceil(lua_State* L)
{
    lua_pushnumber(L, moo_numfromint(moo_numceil(luaL_checknumber(L, 1))));
    return 1;
}

static int math_floor(lua_State* L)
{
    lua_pushnumber(L, moo_numfromint(moo_numfloor(luaL_checknumber(L, 1))));
    return 1;
}

// moo: fmod and modf on the raw Q48.16 value (docs/DESIGN.md "math library", "Division by zero")
static int math_fmod(lua_State* L)
{
    fixed_t a = luaL_checknumber(L, 1);
    fixed_t b = luaL_checknumber(L, 2);
    fixed_t out;
    if (!moo_math_fmod(a, b, &out))
        luaL_error(L, "division by zero");
    lua_pushnumber(L, out);
    return 1;
}

static int math_modf(lua_State* L)
{
    fixed_t ip, fp;
    moo_math_modf(luaL_checknumber(L, 1), &ip, &fp);
    lua_pushnumber(L, ip);
    lua_pushnumber(L, fp);
    return 2;
}

static int math_sqrt(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(sqrt(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

static int math_pow(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(pow(moo_numbridgetodouble(luaL_checknumber(L, 1)), moo_numbridgetodouble(luaL_checknumber(L, 2)))));
    return 1;
}

static int math_log(lua_State* L)
{
    double x = moo_numbridgetodouble(luaL_checknumber(L, 1));
    double res;
    if (lua_isnoneornil(L, 2))
        res = log(x);
    else
    {
        double base = moo_numbridgetodouble(luaL_checknumber(L, 2));
        if (base == 2.0)
            res = log2(x);
        else if (base == 10.0)
            res = log10(x);
        else
            res = log(x) / log(base);
    }
    lua_pushnumber(L, moo_numbridgefromdouble(res));
    return 1;
}

static int math_log10(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(log10(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

static int math_exp(lua_State* L)
{
    lua_pushnumber(L, moo_numbridgefromdouble(exp(moo_numbridgetodouble(luaL_checknumber(L, 1)))));
    return 1;
}

// moo: deg, rad, frexp and ldexp on the raw Q48.16 value (docs/DESIGN.md "math library", "Exact integers")
static int math_deg(lua_State* L)
{
    lua_pushnumber(L, moo_math_deg(luaL_checknumber(L, 1)));
    return 1;
}

static int math_rad(lua_State* L)
{
    lua_pushnumber(L, moo_math_rad(luaL_checknumber(L, 1)));
    return 1;
}

static int math_frexp(lua_State* L)
{
    fixed_t m;
    int e;
    moo_math_frexp(luaL_checknumber(L, 1), &m, &e);
    lua_pushnumber(L, m);
    lua_pushinteger(L, e);
    return 2;
}

static int math_ldexp(lua_State* L)
{
    fixed_t a = luaL_checknumber(L, 1);
    int e = luaL_checkinteger(L, 2);
    lua_pushnumber(L, moo_math_ldexp(a, e));
    return 1;
}

static int math_min(lua_State* L)
{
    int n = lua_gettop(L); // number of arguments
    lua_Number dmin = luaL_checknumber(L, 1);
    int i;
    for (i = 2; i <= n; i++)
    {
        lua_Number d = luaL_checknumber(L, i);
        if (d < dmin)
            dmin = d;
    }
    lua_pushnumber(L, dmin);
    return 1;
}

static int math_max(lua_State* L)
{
    int n = lua_gettop(L); // number of arguments
    lua_Number dmax = luaL_checknumber(L, 1);
    int i;
    for (i = 2; i <= n; i++)
    {
        lua_Number d = luaL_checknumber(L, i);
        if (d > dmax)
            dmax = d;
    }
    lua_pushnumber(L, dmax);
    return 1;
}

static int math_random(lua_State* L)
{
    global_State* g = L->global;
    switch (lua_gettop(L))
    { // check number of arguments
    case 0:
    { // no arguments
        // Using ldexp instead of division for speed & clarity.
        // See http://mumble.net/~campbell/tmp/random_real.c for details on generating doubles from integer ranges.
        // moo: the 16 fraction bits from the top of one draw: every number in [0, 1) equally likely, exact
        lua_pushnumber(L, lua_Number(pcg32_random(&g->rngstate) >> 16)); // number between 0 and 1
        break;
    }
    case 1:
    { // only upper limit
        int u = luaL_checkinteger(L, 1);
        luaL_argcheck(L, 1 <= u, 1, "interval is empty");

        uint64_t x = uint64_t(u) * pcg32_random(&g->rngstate);
        int r = int(1 + (x >> 32));
        lua_pushinteger(L, r); // int between 1 and `u'
        break;
    }
    case 2:
    { // lower and upper limits
        int l = luaL_checkinteger(L, 1);
        int u = luaL_checkinteger(L, 2);
        luaL_argcheck(L, l <= u, 2, "interval is empty");

        uint32_t ul = uint32_t(u) - uint32_t(l);
        luaL_argcheck(L, ul < UINT_MAX, 2, "interval is too large"); // -INT_MIN..INT_MAX interval can result in integer overflow
        uint64_t x = uint64_t(ul + 1) * pcg32_random(&g->rngstate);
        int r = int(l + (x >> 32));
        lua_pushinteger(L, r); // int between `l' and `u'
        break;
    }
    default:
        luaL_error(L, "wrong number of arguments");
    }
    return 1;
}

static int math_randomseed(lua_State* L)
{
    int seed = luaL_checkinteger(L, 1);

    pcg32_seed(&L->global->rngstate, seed);
    return 0;
}

static const unsigned char kPerlinHash[257] = {
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

const float kPerlinGrad[16][3] = {
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

inline float perlin_fade(float t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

inline float perlin_lerp(float t, float a, float b)
{
    return a + t * (b - a);
}

inline float perlin_grad(int hash, float x, float y, float z)
{
    const float* g = kPerlinGrad[hash & 15];
    return g[0] * x + g[1] * y + g[2] * z;
}

static float perlin(float x, float y, float z)
{
    float xflr = floorf(x);
    float yflr = floorf(y);
    float zflr = floorf(z);

    int xi = int(xflr) & 255;
    int yi = int(yflr) & 255;
    int zi = int(zflr) & 255;

    float xf = x - xflr;
    float yf = y - yflr;
    float zf = z - zflr;

    float u = perlin_fade(xf);
    float v = perlin_fade(yf);
    float w = perlin_fade(zf);

    const unsigned char* p = kPerlinHash;

    int a = (p[xi] + yi) & 255;
    int aa = (p[a] + zi) & 255;
    int ab = (p[a + 1] + zi) & 255;

    int b = (p[xi + 1] + yi) & 255;
    int ba = (p[b] + zi) & 255;
    int bb = (p[b + 1] + zi) & 255;

    float la = perlin_lerp(u, perlin_grad(p[aa], xf, yf, zf), perlin_grad(p[ba], xf - 1, yf, zf));
    float lb = perlin_lerp(u, perlin_grad(p[ab], xf, yf - 1, zf), perlin_grad(p[bb], xf - 1, yf - 1, zf));
    float la1 = perlin_lerp(u, perlin_grad(p[aa + 1], xf, yf, zf - 1), perlin_grad(p[ba + 1], xf - 1, yf, zf - 1));
    float lb1 = perlin_lerp(u, perlin_grad(p[ab + 1], xf, yf - 1, zf - 1), perlin_grad(p[bb + 1], xf - 1, yf - 1, zf - 1));

    return perlin_lerp(w, perlin_lerp(v, la, lb), perlin_lerp(v, la1, lb1));
}

static int math_noise(lua_State* L)
{
    int nx, ny, nz;
    double x = moo_numbridgetodouble(lua_tonumberx(L, 1, &nx));
    double y = moo_numbridgetodouble(lua_tonumberx(L, 2, &ny));
    double z = moo_numbridgetodouble(lua_tonumberx(L, 3, &nz));

    luaL_argexpected(L, nx, 1, "number");
    luaL_argexpected(L, ny || lua_isnoneornil(L, 2), 2, "number");
    luaL_argexpected(L, nz || lua_isnoneornil(L, 3), 3, "number");

    if (FFlag::FixMathNoisePrecision)
    {
        // NOTES: input numbers from Luau are double with higher precision and range than float used by perlin().
        // If we don't do this, for large numbers, perlin() will return almost always 0, since with larger inputs,
        // most of the mantissa is used to store the integer part and perlin() is always 0 at integer cell values.
        // Noise repeat exactly every 256 units in all dimensions, so we can wrap to prevent loss of precision for large numbers.
        x = fmod(x, 256.0);
        y = fmod(y, 256.0);
        z = fmod(z, 256.0);
    }

    double r = perlin((float)x, (float)y, (float)z);

    lua_pushnumber(L, moo_numbridgefromdouble(r));
    return 1;
}

static int math_clamp(lua_State* L)
{
    lua_Number v = luaL_checknumber(L, 1);
    lua_Number min = luaL_checknumber(L, 2);
    lua_Number max = luaL_checknumber(L, 3);

    luaL_argcheck(L, min <= max, 3, "max must be greater than or equal to min");

    lua_Number r = v < min ? min : v;
    r = r > max ? max : r;

    lua_pushnumber(L, r);
    return 1;
}

static int math_sign(lua_State* L)
{
    lua_Number v = luaL_checknumber(L, 1);
    lua_pushnumber(L, v > 0 ? MOO_NUMBER_ONE : v < 0 ? -MOO_NUMBER_ONE : 0);
    return 1;
}

static int math_round(lua_State* L)
{
    lua_pushnumber(L, moo_numround(luaL_checknumber(L, 1)));
    return 1;
}

// moo: map, lerp, isnan, isinf and isfinite on fixed (docs/DESIGN.md "math library", "Division by zero")
static int math_map(lua_State* L)
{
    fixed_t x = luaL_checknumber(L, 1);
    fixed_t inmin = luaL_checknumber(L, 2);
    fixed_t inmax = luaL_checknumber(L, 3);
    fixed_t outmin = luaL_checknumber(L, 4);
    fixed_t outmax = luaL_checknumber(L, 5);

    fixed_t denom = moo_numsub(inmax, inmin);
    if (denom == 0)
        luaL_error(L, "division by zero");
    fixed_t result = moo_numadd(outmin, moo_numdiv(moo_nummul(moo_numsub(x, inmin), moo_numsub(outmax, outmin)), denom));
    lua_pushnumber(L, result);
    return 1;
}

static int math_lerp(lua_State* L)
{
    fixed_t a = luaL_checknumber(L, 1);
    fixed_t b = luaL_checknumber(L, 2);
    fixed_t t = luaL_checknumber(L, 3);

    lua_pushnumber(L, moo_math_lerp(a, b, t));
    return 1;
}

static int math_isnan(lua_State* L)
{
    (void)luaL_checknumber(L, 1);

    lua_pushboolean(L, 0);
    return 1;
}

static int math_isinf(lua_State* L)
{
    (void)luaL_checknumber(L, 1);

    lua_pushboolean(L, 0);
    return 1;
}

static int math_isfinite(lua_State* L)
{
    (void)luaL_checknumber(L, 1);

    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg mathlib[] = {
    {"abs", math_abs},
    {"acos", math_acos},
    {"asin", math_asin},
    {"atan2", math_atan2},
    {"atan", math_atan},
    {"ceil", math_ceil},
    {"cosh", math_cosh},
    {"cos", math_cos},
    {"deg", math_deg},
    {"exp", math_exp},
    {"floor", math_floor},
    {"fmod", math_fmod},
    {"frexp", math_frexp},
    {"ldexp", math_ldexp},
    {"log10", math_log10},
    {"log", math_log},
    {"max", math_max},
    {"min", math_min},
    {"modf", math_modf},
    {"pow", math_pow},
    {"rad", math_rad},
    {"random", math_random},
    {"randomseed", math_randomseed},
    {"sinh", math_sinh},
    {"sin", math_sin},
    {"sqrt", math_sqrt},
    {"tanh", math_tanh},
    {"tan", math_tan},
    {"noise", math_noise},
    {"clamp", math_clamp},
    {"sign", math_sign},
    {"round", math_round},
    {"map", math_map},
    {"lerp", math_lerp},
    {"isnan", math_isnan},
    {"isinf", math_isinf},
    {"isfinite", math_isfinite},
    {NULL, NULL},
};

/*
** Open math library
*/
int luaopen_math(lua_State* L)
{
    // moo: the seed is a host input (moo_setrandomseed), never an address or a clock
    pcg32_seed(&L->global->rngstate, L->global->randomseed);

    luaL_register(L, LUA_MATHLIBNAME, mathlib);

    // moo: exact Q48.16 constants; math.pi is nearest Q48.16 to pi, math.huge is MOO_NUMBER_MAX (docs/DESIGN.md "math library")
    lua_pushnumber(L, MOO_MATH_PI);
    lua_setfield(L, -2, "pi");
    lua_pushnumber(L, MOO_NUMBER_MAX);
    lua_setfield(L, -2, "huge");
    lua_pushnumber(L, fixed_t(0));
    lua_setfield(L, -2, "nan");
    lua_pushnumber(L, MOO_MATH_E);
    lua_setfield(L, -2, "e");
    lua_pushnumber(L, MOO_MATH_PHI);
    lua_setfield(L, -2, "phi");
    lua_pushnumber(L, MOO_MATH_SQRT2);
    lua_setfield(L, -2, "sqrt2");
    lua_pushnumber(L, MOO_MATH_TAU);
    lua_setfield(L, -2, "tau");

    return 1;
}

void moo_setrandomseed(lua_State* L, uint64_t seed)
{
    L->global->randomseed = seed;
    pcg32_seed(&L->global->rngstate, seed);
}
