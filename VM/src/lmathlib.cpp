// below and the fixed trig functions share their integer implementations with bytecode fastcalls.
// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lualib.h"

#include "lstate.h"
#include "moo.h"

#include "moo_pow.h"

#include "fixed/fixed_math.h"

#include <limits>
#include <math.h>

// moo: Q48.16 constants, each rounded once to the nearest 2^-16
#define MOO_MATH_PI (fixed_t(205887))          // round(pi * 2^16): 3.14159265358979323846 * 65536 = 205887.416...
#define MOO_MATH_DEG_PER_RAD (fixed_t(3754936)) // round((180 / pi) * 2^16): 57.29577951308232... * 65536 = 3754936.206...
#define MOO_MATH_RAD_PER_DEG (fixed_t(1144))    // round((pi / 180) * 2^16): 0.017453292519943295... * 65536 = 1143.818...
#define MOO_MATH_E (fixed_t(178145))            // round(e * 2^16): 2.71828182845904523536 * 65536 = 178145.382...
#define MOO_MATH_PHI (fixed_t(106039))          // round(phi * 2^16): 1.61803398874989484820 * 65536 = 106039.275...
#define MOO_MATH_SQRT2 (fixed_t(92682))         // round(sqrt(2) * 2^16): 1.41421356237309504880 * 65536 = 92681.900...
#define MOO_MATH_TAU (fixed_t(411775))          // round(2 * pi * 2^16): 6.28318530717958647692 * 65536 = 411774.832...
#define MOO_MATH_LOG2_E (fixed_t(94548))        // round((1 / ln(2)) * 2^16): 1.44269504088896340736 * 65536 = 94548.462...
#define MOO_MATH_INV_LOG2_10 (fixed_t(19728))   // round((ln(2) / ln(10)) * 2^16): 0.30102999566398119521 * 65536 = 19728.302...

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

// moo: the trig functions on fixed, shared with the fastcalls in lbuiltins.cpp so both give the same bits
// (docs/DESIGN.md "math library"). false is an error the library raises and the fastcall falls back to: tan on a
// zero cosine, asin and acos when |x| > 1.
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

fixed_t moo_math_atan2(fixed_t y, fixed_t x)
{
    return fixAtan2(y, x);
}

fixed_t moo_math_atan(fixed_t x)
{
    return fixAtan2(x, MOO_NUMBER_ONE);
}

bool moo_math_asin(fixed_t x, fixed_t* out)
{
    if (x < -MOO_NUMBER_ONE || x > MOO_NUMBER_ONE)
        return false;
    *out = fixAtan2(x, fixSqrt(MOO_NUMBER_ONE - fixMul(x, x)));
    return true;
}

bool moo_math_acos(fixed_t x, fixed_t* out)
{
    if (x < -MOO_NUMBER_ONE || x > MOO_NUMBER_ONE)
        return false;
    *out = fixAtan2(fixSqrt(MOO_NUMBER_ONE - fixMul(x, x)), x);
    return true;
}

// moo: fmod, modf, frexp, ldexp, deg, rad and lerp on the raw value, shared with the fastcalls
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

// moo: exp and the hyperbolic functions, shared with the fastcalls.
// Scaling uses fixMul, which rounds half up once; fixExp2 rounds its dropped bits half up and saturates.
// sinh/cosh evaluate exp(+/-x)/2 before the saturating sum/difference. tanh uses
// sign(x) * (1 - exp(-2|x|)) / (1 + exp(-2|x|)); its final fixDiv truncates toward zero.
fixed_t moo_math_exp(fixed_t x)
{
    return fixExp2(fixMul(x, MOO_MATH_LOG2_E));
}

static fixed_t moo_math_halfexp(fixed_t x)
{
    return fixExp2(moo_numsub(fixMul(x, MOO_MATH_LOG2_E), MOO_NUMBER_ONE));
}

fixed_t moo_math_sinh(fixed_t x)
{
    return moo_numsub(moo_math_halfexp(x), moo_math_halfexp(moo_numunm(x)));
}

fixed_t moo_math_cosh(fixed_t x)
{
    return moo_numadd(moo_math_halfexp(x), moo_math_halfexp(moo_numunm(x)));
}

fixed_t moo_math_tanh(fixed_t x)
{
    fixed_t magnitude = x < 0 ? moo_numunm(x) : x;
    fixed_t twice = moo_numadd(magnitude, magnitude);
    fixed_t inverse = moo_math_exp(moo_numunm(twice));
    fixed_t result = moo_numdiv(moo_numsub(MOO_NUMBER_ONE, inverse), moo_numadd(MOO_NUMBER_ONE, inverse));
    return x < 0 ? moo_numunm(result) : result;
}

// moo: log uses fixLog2; log divides by rounded log2(e) with fixDiv
// (toward zero), log10 multiplies by rounded 1/log2(10) with fixMul (half up), and a custom base
// divides the two fixed log2 results with fixDiv (toward zero). Domain refusals stay in the library caller.
fixed_t moo_math_log10(fixed_t x)
{
    return fixMul(fixLog2(x), MOO_MATH_INV_LOG2_10);
}

bool moo_math_log(fixed_t x, bool hasBase, fixed_t base, fixed_t* out)
{
    if (x <= 0 || (hasBase && base <= 0) || (hasBase && base == MOO_NUMBER_ONE))
        return false;

    fixed_t logx = fixLog2(x);
    if (!hasBase)
        *out = moo_numdiv(logx, MOO_MATH_LOG2_E);
    else if (base == 2 * MOO_NUMBER_ONE)
        *out = logx;
    else if (base == 10 * MOO_NUMBER_ONE)
        *out = moo_math_log10(x);
    else
        *out = moo_numdiv(logx, fixLog2(base));
    return true;
}

static int math_sin(lua_State* L)
{
    lua_pushnumber(L, moo_math_sin(luaL_checknumber(L, 1)));
    return 1;
}

// moo: shared with the fastcall
static int math_sinh(lua_State* L)
{
    lua_pushnumber(L, moo_math_sinh(luaL_checknumber(L, 1)));
    return 1;
}

static int math_cos(lua_State* L)
{
    lua_pushnumber(L, moo_math_cos(luaL_checknumber(L, 1)));
    return 1;
}

// moo: shared with the fastcall
static int math_cosh(lua_State* L)
{
    lua_pushnumber(L, moo_math_cosh(luaL_checknumber(L, 1)));
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

// moo: shared with the fastcall
static int math_tanh(lua_State* L)
{
    lua_pushnumber(L, moo_math_tanh(luaL_checknumber(L, 1)));
    return 1;
}

static int math_asin(lua_State* L)
{
    fixed_t out;
    if (!moo_math_asin(luaL_checknumber(L, 1), &out))
        luaL_error(L, "math.asin domain");
    lua_pushnumber(L, out);
    return 1;
}

static int math_acos(lua_State* L)
{
    fixed_t out;
    if (!moo_math_acos(luaL_checknumber(L, 1), &out))
        luaL_error(L, "math.acos domain");
    lua_pushnumber(L, out);
    return 1;
}

static int math_atan(lua_State* L)
{
    lua_pushnumber(L, moo_math_atan(luaL_checknumber(L, 1)));
    return 1;
}

static int math_atan2(lua_State* L)
{
    fixed_t y = luaL_checknumber(L, 1);
    fixed_t x = luaL_checknumber(L, 2);
    lua_pushnumber(L, moo_math_atan2(y, x));
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

// moo: on the raw value
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

// moo: fixed's fixSqrt, exact, rounded toward zero; negative is a named error (docs/history/layer4/moo-l4-math-sqrt.md)
static int math_sqrt(lua_State* L)
{
    lua_Number a = luaL_checknumber(L, 1);
    if (a < 0)
        luaL_error(L, "math.sqrt of a negative number");
    lua_pushnumber(L, fixSqrt(a));
    return 1;
}

static int math_pow(lua_State* L)
{
    fixed_t base = luaL_checknumber(L, 1);
    fixed_t exponent = luaL_checknumber(L, 2);

    // moo: an integer exponent is moo_pow_int, exact, rounded once; this raises its errors
    if (moo_numisinteger(exponent))
    {
        fixed_t out;
        int status = moo_pow_int(base, exponent >> MOO_NUMBER_FRACTION_BITS, &out);

        if (status == MOO_POW_DIVISION_BY_ZERO)
            luaL_error(L, "division by zero");
#if MOO_OVERFLOW_ERRORS
        if (status == MOO_POW_SATURATED)
            luaL_error(L, "number overflow in '^'");
#endif
        lua_pushnumber(L, out);
        return 1;
    }

    // moo: a fractional exponent is fixPow = fixExp2(exponent * fixLog2(base)); a negative base is the named
    // error, and 0 ^ positive fraction is 0
    if (base < 0)
        luaL_error(L, "pow of a negative number to a fractional power");
    lua_pushnumber(L, fixPow(base, exponent));
    return 1;
}

// moo: shared with the fastcall
static int math_log(lua_State* L)
{
    fixed_t x = luaL_checknumber(L, 1);
    bool hasBase = !lua_isnoneornil(L, 2);
    fixed_t base = hasBase ? luaL_checknumber(L, 2) : 0;
    if (x <= 0 || (hasBase && base <= 0))
        luaL_error(L, "math.log of a non-positive number");
    if (hasBase && base == MOO_NUMBER_ONE)
        luaL_error(L, "division by zero");
    fixed_t result;
    if (!moo_math_log(x, hasBase, base, &result))
        luaL_error(L, "math.log of a non-positive number");
    lua_pushnumber(L, result);
    return 1;
}

// moo: shared with the fastcall
static int math_log10(lua_State* L)
{
    fixed_t x = luaL_checknumber(L, 1);
    if (x <= 0)
        luaL_error(L, "math.log of a non-positive number");
    lua_pushnumber(L, moo_math_log10(x));
    return 1;
}

// moo: shared with the fastcall
static int math_exp(lua_State* L)
{
    lua_pushnumber(L, moo_math_exp(luaL_checknumber(L, 1)));
    return 1;
}

// moo: on the raw value
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

// moo: Perlin noise on Q48.16, in the operation order docs/DESIGN.md "math library" gives
static const int kPerlinGrad[16][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
    {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
    {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}
};

static fixed_t perlin_fade(fixed_t t)
{
    fixed_t six = moo_numfromint(6);
    fixed_t fifteen = moo_numfromint(15);
    fixed_t ten = moo_numfromint(10);
    fixed_t inner = moo_numadd(moo_nummul(t, moo_numsub(moo_nummul(t, six), fifteen)), ten);
    return moo_nummul(moo_nummul(moo_nummul(t, t), t), inner);
}

static fixed_t perlin_lerp(fixed_t t, fixed_t a, fixed_t b)
{
    return moo_numadd(a, moo_nummul(t, moo_numsub(b, a)));
}

static fixed_t perlin_grad(int hash, fixed_t x, fixed_t y, fixed_t z)
{
    const int* g = kPerlinGrad[hash & 15];
    fixed_t gx = g[0] < 0 ? moo_numunm(x) : (g[0] ? x : 0);
    fixed_t gy = g[1] < 0 ? moo_numunm(y) : (g[1] ? y : 0);
    fixed_t gz = g[2] < 0 ? moo_numunm(z) : (g[2] ? z : 0);
    return moo_numadd(moo_numadd(gx, gy), gz);
}

fixed_t moo_math_noise(fixed_t x, fixed_t y, fixed_t z)
{
    // moo: the 256-cell period keeps the table index bounded and preserves negative inputs exactly.
    const fixed_t period = moo_numfromint(256);
    x = moo_nummod(x, period);
    y = moo_nummod(y, period);
    z = moo_nummod(z, period);

    int xi = int(moo_numfloor(x)) & 255;
    int yi = int(moo_numfloor(y)) & 255;
    int zi = int(moo_numfloor(z)) & 255;

    fixed_t xf = moo_numsub(x, moo_numfromint(moo_numfloor(x)));
    fixed_t yf = moo_numsub(y, moo_numfromint(moo_numfloor(y)));
    fixed_t zf = moo_numsub(z, moo_numfromint(moo_numfloor(z)));

    fixed_t u = perlin_fade(xf);
    fixed_t v = perlin_fade(yf);
    fixed_t w = perlin_fade(zf);

    const unsigned char* p = kPerlinHash;
    int a = (p[xi] + yi) & 255;
    int aa = (p[a] + zi) & 255;
    int ab = (p[a + 1] + zi) & 255;
    int b = (p[xi + 1] + yi) & 255;
    int ba = (p[b] + zi) & 255;
    int bb = (p[b + 1] + zi) & 255;

    fixed_t la = perlin_lerp(u, perlin_grad(p[aa], xf, yf, zf), perlin_grad(p[ba], moo_numsub(xf, MOO_NUMBER_ONE), yf, zf));
    fixed_t lb = perlin_lerp(u, perlin_grad(p[ab], xf, moo_numsub(yf, MOO_NUMBER_ONE), zf), perlin_grad(p[bb], moo_numsub(xf, MOO_NUMBER_ONE), moo_numsub(yf, MOO_NUMBER_ONE), zf));
    fixed_t la1 = perlin_lerp(u, perlin_grad(p[aa + 1], xf, yf, moo_numsub(zf, MOO_NUMBER_ONE)), perlin_grad(p[ba + 1], moo_numsub(xf, MOO_NUMBER_ONE), yf, moo_numsub(zf, MOO_NUMBER_ONE)));
    fixed_t lb1 = perlin_lerp(u, perlin_grad(p[ab + 1], xf, moo_numsub(yf, MOO_NUMBER_ONE), moo_numsub(zf, MOO_NUMBER_ONE)), perlin_grad(p[bb + 1], moo_numsub(xf, MOO_NUMBER_ONE), moo_numsub(yf, MOO_NUMBER_ONE), moo_numsub(zf, MOO_NUMBER_ONE)));

    return perlin_lerp(w, perlin_lerp(v, la, lb), perlin_lerp(v, la1, lb1));
}

static int math_noise(lua_State* L)
{
    int nx, ny, nz;
    fixed_t x = lua_tonumberx(L, 1, &nx);
    fixed_t y = lua_tonumberx(L, 2, &ny);
    fixed_t z = lua_tonumberx(L, 3, &nz);

    luaL_argexpected(L, nx, 1, "number");
    luaL_argexpected(L, ny || lua_isnoneornil(L, 2), 2, "number");
    luaL_argexpected(L, nz || lua_isnoneornil(L, 3), 3, "number");

    lua_pushnumber(L, moo_math_noise(x, y, z));
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

// moo: map, lerp, isnan, isinf and isfinite on fixed
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

    // moo: math.pi is the nearest Q48.16 to pi; math.huge is MOO_NUMBER_MAX
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
