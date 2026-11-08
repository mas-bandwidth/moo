// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/BuiltinDefinitions.h"

LUAU_FASTFLAG(LuauIntegerLibrary)
LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(LuauAllowGlobalDeclarationToBeCalledClass)
LUAU_FASTFLAG(DebugLuauExactTableTypes)
LUAU_FASTFLAG(DebugLuauUserDefinedClasses)
LUAU_FASTFLAGVARIABLE(DebugLuauCoroutineFinallyAnalysis)

namespace Luau
{

static constexpr const char* kBuiltinDefinitionBaseSrc = R"BUILTIN_SRC(

@checked declare function require(target: any): any

@checked declare function getfenv(target: any): { [string]: any }

declare _G: any
declare _VERSION: string

declare function gcinfo(): number

declare function print<T...>(...: T...)

declare function type<T>(value: T): string
declare function typeof<T>(value: T): string

-- `assert` has a magic function attached that will give more detailed type information
declare function assert<T>(value: T, errorMessage: string?): T
declare function error<T>(message: T, level: number?): never

declare function tostring<T>(value: T): string
declare function tonumber<T>(value: T, radix: number?): number?

declare function rawequal<T1, T2>(a: T1, b: T2): boolean
declare function rawget<K, V>(tab: {[K]: V}, k: K): V?
declare function rawset<K, V>(tab: {[K]: V}, k: K, v: V): {[K]: V}
declare function rawlen<K, V>(obj: {[K]: V} | string): number

declare function setfenv<T..., R...>(target: number | (T...) -> R..., env: {[string]: any}): ((T...) -> R...)?

declare function ipairs<V>(tab: {V}): (({V}, number) -> (number?, V), {V}, number)

declare function pcall<A..., R...>(f: (A...) -> R..., ...: A...): (boolean, R...)

-- FIXME: The actual type of `xpcall` is:
-- <E, A..., R1..., R2...>(f: (A...) -> R1..., err: (E) -> R2..., A...) -> (true, R1...) | (false, R2...)
-- Since we can't represent the return value, we use (boolean, R1...).
declare function xpcall<E, A..., R1..., R2...>(f: (A...) -> R1..., err: (E) -> R2..., ...: A...): (boolean, R1...)

-- `select` has a magic function attached to provide more detailed type information
declare function select<A...>(i: string | number, ...: A...): ...any

-- FIXME: This type is not entirely correct - `loadstring` returns a function or
-- (nil, string).
declare function loadstring<A...>(src: string, chunkname: string?): (((A...) -> any)?, string?)

@checked declare function newproxy(mt: boolean?): any

-- Cannot use `typeof` here because it will produce a polytype when we expect a monotype.
declare function unpack<V>(tab: {V}, i: number?, j: number?): ...V

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionBit32Src = R"BUILTIN_SRC(

declare bit32: {
    band: @checked (...number) -> number,
    bor: @checked (...number) -> number,
    bxor: @checked (...number) -> number,
    btest: @checked (number, ...number) -> boolean,
    rrotate: @checked (x: number, disp: number) -> number,
    lrotate: @checked (x: number, disp: number) -> number,
    lshift: @checked (x: number, disp: number) -> number,
    arshift: @checked (x: number, disp: number) -> number,
    rshift: @checked (x: number, disp: number) -> number,
    bnot: @checked (x: number) -> number,
    extract: @checked (n: number, field: number, width: number?) -> number,
    replace: @checked (n: number, v: number, field: number, width: number?) -> number,
    countlz: @checked (n: number) -> number,
    countrz: @checked (n: number) -> number,
    byteswap: @checked (n: number) -> number,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionMathSrc = R"BUILTIN_SRC(

declare math: {
    frexp: @checked (n: number) -> (number, number),
    ldexp: @checked (s: number, e: number) -> number,
    fmod: @checked (x: number, y: number) -> number,
    modf: @checked (n: number) -> (number, number),
    pow: @checked (x: number, y: number) -> number,
    exp: @checked (n: number) -> number,

    ceil: @checked (n: number) -> number,
    floor: @checked (n: number) -> number,
    abs: @checked (n: number) -> number,
    sqrt: @checked (n: number) -> number,

    log: @checked (n: number, base: number?) -> number,
    log10: @checked (n: number) -> number,

    rad: @checked (n: number) -> number,
    deg: @checked (n: number) -> number,

    sin: @checked (n: number) -> number,
    cos: @checked (n: number) -> number,
    tan: @checked (n: number) -> number,
    sinh: @checked (n: number) -> number,
    cosh: @checked (n: number) -> number,
    tanh: @checked (n: number) -> number,
    atan: @checked (n: number) -> number,
    acos: @checked (n: number) -> number,
    asin: @checked (n: number) -> number,
    atan2: @checked (y: number, x: number) -> number,

    min: @checked (number, ...number) -> number,
    max: @checked (number, ...number) -> number,

    pi: number,
    huge: number,
    nan: number,
    e: number,
    phi: number,
    sqrt2: number,
    tau: number,

    randomseed: @checked (seed: number) -> (),
    random: @checked (number?, number?) -> number,

    sign: @checked (n: number) -> number,
    clamp: @checked (n: number, min: number, max: number) -> number,
    noise: @checked (x: number, y: number?, z: number?) -> number,
    round: @checked (n: number) -> number,
    map: @checked (x: number, inmin: number, inmax: number, outmin: number, outmax: number) -> number,
    lerp: @checked (a: number, b: number, t: number) -> number,

    isnan: @checked (x: number) -> boolean,
    isinf: @checked (x: number) -> boolean,
    isfinite: @checked (x: number) -> boolean,

    muldiv: @checked (a: number, b: number, c: number) -> number,
    halfsum: @checked (a: number, b: number) -> number,
    halfdifference: @checked (a: number, b: number) -> number,
    unwindangle: @checked (radians: number) -> number,
    cossin: @checked (radians: number) -> (number, number),
    smoothcriticallydamped: @checked (current: number, target: number, velocity: number, smoothTime: number, deltaTime: number) -> (number, number),
    smoothcriticallydampedupdown: @checked (current: number, target: number, velocity: number, smoothTimeUp: number, smoothTimeDown: number, deltaTime: number) -> (number, number),
    fits: @checked (raw: number, minRaw: number, maxRaw: number) -> boolean,
    narrow: @checked (raw: number, shift: number) -> number,
    widen: @checked (raw: number, shift: number) -> number,
}

)BUILTIN_SRC";

[[maybe_unused]] static constexpr const char* kBuiltinDefinitionOsSrc = R"BUILTIN_SRC(

type DateTypeArg = {
    year: number,
    month: number,
    day: number,
    hour: number?,
    min: number?,
    sec: number?,
    isdst: boolean?,
}

type DateTypeResult = {
    year: number,
    month: number,
    wday: number,
    yday: number,
    day: number,
    hour: number,
    min: number,
    sec: number,
    isdst: boolean,
}

declare os: {
    time: (time: DateTypeArg?) -> number,
    date: ((formatString: "*t" | "!*t", time: number?) -> DateTypeResult) & ((formatString: string?, time: number?) -> string),
    difftime: (t2: DateTypeResult | number, t1: DateTypeResult | number) -> number,
    clock: () -> number,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionCoroutineSrc_DEPRECATED = R"BUILTIN_SRC(

declare coroutine: {
    create: <A..., R...>(f: (A...) -> R...) -> thread,
    resume: <A..., R...>(co: thread, A...) -> (boolean, R...),
    running: () -> thread,
    status: @checked (co: thread) -> "dead" | "running" | "normal" | "suspended",
    wrap: <A..., R...>(f: (A...) -> R...) -> ((A...) -> R...),
    yield: <A..., R...>(A...) -> R...,
    isyieldable: () -> boolean,
    close: @checked (co: thread) -> (boolean, any)
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionCoroutineSrc = R"BUILTIN_SRC(

declare coroutine: {
    create: <A..., R...>(f: (A...) -> R...) -> thread,
    resume: <A..., R...>(co: thread, A...) -> (boolean, R...),
    running: () -> thread,
    status: @checked (co: thread) -> "dead" | "running" | "normal" | "suspended",
    wrap: <A..., R...>(f: (A...) -> R...) -> ((A...) -> R...),
    yield: <A..., R...>(A...) -> R...,
    isyieldable: () -> boolean,
    close: @checked (co: thread) -> (boolean, any),
    finally: (co: thread, callback: (status: "finished" | "error" | "cancelled", ...any) -> ()) -> ()
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionTableSrc = R"BUILTIN_SRC(

declare table: {
    concat: <V>(t: {V}, sep: string?, i: number?, j: number?) -> string,
    insert: (<V>(t: {V}, value: V) -> ()) & (<V>(t: {V}, pos: number, value: V) -> ()),
    maxn: <V>(t: {V}) -> number,
    remove: <V>(t: {V}, number?) -> V?,
    sort: <V>(t: {V}, comp: ((V, V) -> boolean)?) -> (),
    create: <V>(count: number, value: V?) -> {V},
    find: <V>(haystack: {V}, needle: V, init: number?) -> number?,

    unpack: <V>(list: {V}, i: number?, j: number?) -> ...V,
    pack: <V>(...V) -> { n: number, [number]: V },

    getn: <V>(t: {V}) -> number,
    foreach: <K, V>(t: {[K]: V}, f: (K, V) -> ()) -> (),
    foreachi: <V>({V}, (number, V) -> ()) -> (),

    move: <V>(src: {V}, a: number, b: number, t: number, dst: {V}?) -> {V},

    clear: (table: {}) -> (),
    isfrozen: (t: {}) -> boolean,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionTableSrc_EXACT_TABLES = R"BUILTIN_SRC(

declare table: {
    concat: <V>(t: {V, ...}, sep: string?, i: number?, j: number?) -> string,
    insert: (<V>(t: {V, ...}, value: V) -> ()) & (<V>(t: {V, ...}, pos: number, value: V) -> ()),
    maxn: <V>(t: {V, ...}) -> number,
    remove: <V>(t: {V, ...}, number?) -> V?,
    sort: <V>(t: {V, ...}, comp: ((V, V) -> boolean)?) -> (),
    create: <V>(count: number, value: V?) -> {V},
    find: <V>(haystack: {V, ...}, needle: V, init: number?) -> number?,

    unpack: <V>(list: {V, ...}, i: number?, j: number?) -> ...V,
    pack: <V>(...V) -> { n: number, [number]: V },

    getn: <V>(t: {V, ...}) -> number,
    foreach: <K, V>(t: {[K]: V, ...}, f: (K, V) -> ()) -> (),
    foreachi: <V>({V, ...}, (number, V) -> ()) -> (),

    move: <V>(src: {V, ...}, a: number, b: number, t: number, dst: {V, ...}?) -> {V, ...},

    clear: (table: {...}) -> (),
    isfrozen: (t: {...}) -> boolean,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionDebugSrc = R"BUILTIN_SRC(

declare debug: {
    info: ((thread: thread, level: number, options: string) -> ...any) & ((level: number, options: string) -> ...any) & (<A..., R1...>(func: (A...) -> R1..., options: string) -> ...any),
    traceback: ((message: string?, level: number?) -> string) & ((thread: thread, message: string?, level: number?) -> string),
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionUtf8Src = R"BUILTIN_SRC(

declare utf8: {
    char: @checked (...number) -> string,
    charpattern: string,
    codes: @checked (str: string) -> ((string, number) -> (number, number), string, number),
    codepoint: @checked (str: string, i: number?, j: number?) -> ...number,
    len: @checked (s: string, i: number?, j: number?) -> (number?, number?),
    offset: @checked (s: string, n: number?, i: number?) -> number,
}

)BUILTIN_SRC";

[[maybe_unused]] static constexpr const char* kBuiltinDefinitionBufferSrc = R"BUILTIN_SRC(
--- Buffer API
declare buffer: {
    create: @checked (size: number) -> buffer,
    fromstring: @checked (str: string) -> buffer,
    tostring: @checked (b: buffer) -> string,
    len: @checked (b: buffer) -> number,
    copy: @checked (target: buffer, targetOffset: number, source: buffer, sourceOffset: number?, count: number?) -> (),
    fill: @checked (b: buffer, offset: number, value: number, count: number?) -> (),
    readi8: @checked (b: buffer, offset: number) -> number,
    readu8: @checked (b: buffer, offset: number) -> number,
    readi16: @checked (b: buffer, offset: number) -> number,
    readu16: @checked (b: buffer, offset: number) -> number,
    readi32: @checked (b: buffer, offset: number) -> number,
    readu32: @checked (b: buffer, offset: number) -> number,
    readf32: @checked (b: buffer, offset: number) -> number,
    readf64: @checked (b: buffer, offset: number) -> number,
    writei8: @checked (b: buffer, offset: number, value: number) -> (),
    writeu8: @checked (b: buffer, offset: number, value: number) -> (),
    writei16: @checked (b: buffer, offset: number, value: number) -> (),
    writeu16: @checked (b: buffer, offset: number, value: number) -> (),
    writei32: @checked (b: buffer, offset: number, value: number) -> (),
    writeu32: @checked (b: buffer, offset: number, value: number) -> (),
    writef32: @checked (b: buffer, offset: number, value: number) -> (),
    writef64: @checked (b: buffer, offset: number, value: number) -> (),
    readstring: @checked (b: buffer, offset: number, count: number) -> string,
    writestring: @checked (b: buffer, offset: number, value: string, count: number?) -> (),
    readbits: @checked (b: buffer, bitOffset: number, bitCount: number) -> number,
    writebits: @checked (b: buffer, bitOffset: number, bitCount: number, value: number) -> (),
    readinteger: @checked (b: buffer, offset: number) -> integer,
    writeinteger: @checked (b: buffer, offset: number, value: integer) -> (),
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionBufferSrc_NOINTEGER = R"BUILTIN_SRC(
--- Buffer API
declare buffer: {
    create: @checked (size: number) -> buffer,
    fromstring: @checked (str: string) -> buffer,
    tostring: @checked (b: buffer) -> string,
    len: @checked (b: buffer) -> number,
    copy: @checked (target: buffer, targetOffset: number, source: buffer, sourceOffset: number?, count: number?) -> (),
    fill: @checked (b: buffer, offset: number, value: number, count: number?) -> (),
    readi8: @checked (b: buffer, offset: number) -> number,
    readu8: @checked (b: buffer, offset: number) -> number,
    readi16: @checked (b: buffer, offset: number) -> number,
    readu16: @checked (b: buffer, offset: number) -> number,
    readi32: @checked (b: buffer, offset: number) -> number,
    readu32: @checked (b: buffer, offset: number) -> number,
    readf32: @checked (b: buffer, offset: number) -> number,
    readf64: @checked (b: buffer, offset: number) -> number,
    writei8: @checked (b: buffer, offset: number, value: number) -> (),
    writeu8: @checked (b: buffer, offset: number, value: number) -> (),
    writei16: @checked (b: buffer, offset: number, value: number) -> (),
    writeu16: @checked (b: buffer, offset: number, value: number) -> (),
    writei32: @checked (b: buffer, offset: number, value: number) -> (),
    writeu32: @checked (b: buffer, offset: number, value: number) -> (),
    writef32: @checked (b: buffer, offset: number, value: number) -> (),
    writef64: @checked (b: buffer, offset: number, value: number) -> (),
    readstring: @checked (b: buffer, offset: number, count: number) -> string,
    writestring: @checked (b: buffer, offset: number, value: string, count: number?) -> (),
    readbits: @checked (b: buffer, bitOffset: number, bitCount: number) -> number,
    writebits: @checked (b: buffer, bitOffset: number, bitCount: number, value: number) -> ()
}

)BUILTIN_SRC";

[[maybe_unused]] static const char* const kBuiltinDefinitionVectorSrc = R"BUILTIN_SRC(

-- While vector would have been better represented as a built-in primitive type, type solver extern type handling covers most of the properties
declare extern type vector with
    read x: number
    read y: number
    read z: number
end

declare vector: {
    create: @checked (x: number, y: number, z: number?) -> vector,
    magnitude: @checked (vec: vector) -> number,
    normalize: @checked (vec: vector) -> vector,
    cross: @checked (vec1: vector, vec2: vector) -> vector,
    dot: @checked (vec1: vector, vec2: vector) -> number,
    angle: @checked (vec1: vector, vec2: vector, axis: vector?) -> number,
    floor: @checked (vec: vector) -> vector,
    ceil: @checked (vec: vector) -> vector,
    abs: @checked (vec: vector) -> vector,
    sign: @checked (vec: vector) -> vector,
    clamp: @checked (vec: vector, min: vector, max: vector) -> vector,
    max: @checked (vector, ...vector) -> vector,
    min: @checked (vector, ...vector) -> vector,
    lerp: @checked (vec1: vector, vec2: vector, t: number) -> vector,

    zero: vector,
    one: vector,
}

)BUILTIN_SRC";

// moo: the fixed-point value types of layer 3b and their libraries (docs/DESIGN.md "Analyzer", "vec3, quat and
// transform (layer 3b)"; docs/history/layer3b/moo-v2-l3b-analyzer-types.md). Each type is declared the way upstream declares
// vector, as an extern type, with exactly the components and operators the VM implements (VM/src/lvmutils.cpp,
// luaV_doarithimpl and luaV_gettable) and each library with the VM's functions and argument counts
// (VM/src/moo_vec3lib.cpp, moo_quatlib.cpp, moo_transformlib.cpp; layer 3c's vec2, mat3, plane, aabb and fix30 from
// VM/src/moo_narrowlib.cpp, docs/DESIGN.md "Layer 3c as built"). quat.slerp is left out: the VM refuses it by name
// until fixed has a slerp, so the analyzer reports it as missing rather than promising a result.
static const char* const kBuiltinDefinitionMooValueTypesSrc = R"BUILTIN_SRC(

declare extern type vec3 with
    read x: number
    read y: number
    read z: number
    read X: number
    read Y: number
    read Z: number
    function __add(self, other: vec3): vec3
    function __sub(self, other: vec3): vec3
    function __mul(self, other: vec3): vec3
    function __mul(self, other: number): vec3
    function __unm(self): vec3
end

declare extern type quat with
    read x: number
    read y: number
    read z: number
    read w: number
    read X: number
    read Y: number
    read Z: number
    read W: number
    function __mul(self, other: quat): quat
    function __mul(self, other: vec3): vec3
end

declare extern type transform with
    read p: vec3
    read q: quat
    function __mul(self, other: transform): transform
    function __mul(self, other: vec3): vec3
end

declare extern type vec2 with
    read x: number
    read y: number
    read X: number
    read Y: number
end

declare extern type mat3 with
    read cx: vec3
    read cy: vec3
    read cz: vec3
    function __add(self, other: mat3): mat3
    function __sub(self, other: mat3): mat3
    function __mul(self, other: mat3): mat3
    function __mul(self, other: vec3): vec3
    function __mul(self, other: number): mat3
    function __unm(self): mat3
end

declare extern type plane with
    read normal: vec3
    read offset: number
end

declare extern type aabb with
    read lower: vec3
    read upper: vec3
end

declare extern type fix30 with
end

declare vec3: {
    new: @checked (x: number?, y: number?, z: number?) -> vec3,
    create: @checked (x: number?, y: number?, z: number?) -> vec3,
    dot: @checked (a: vec3, b: vec3) -> number,
    cross: @checked (a: vec3, b: vec3) -> vec3,
    length: @checked (a: vec3) -> number,
    magnitude: @checked (a: vec3) -> number,
    normalize: @checked (a: vec3) -> vec3,
    distance: @checked (a: vec3, b: vec3) -> number,
    distancesquared: @checked (a: vec3, b: vec3) -> number,
    lengthsquared: @checked (a: vec3) -> number,
    lerp: @checked (a: vec3, b: vec3, alpha: number) -> vec3,
    min: @checked (a: vec3, b: vec3) -> vec3,
    max: @checked (a: vec3, b: vec3) -> vec3,
    clamp: @checked (a: vec3, lower: vec3, upper: vec3) -> vec3,
    abs: @checked (a: vec3) -> vec3,
    sign: @checked (a: vec3) -> vec3,
    perp: @checked (a: vec3) -> vec3,
    muladd: @checked (a: vec3, s: number, b: vec3) -> vec3,
    mulsub: @checked (a: vec3, s: number, b: vec3) -> vec3,
    blend2: @checked (s: number, a: vec3, t: number, b: vec3) -> vec3,
    safescale: @checked (a: vec3) -> vec3,
    isnormalized: @checked (a: vec3) -> boolean,
    getlengthandnormalize: @checked (a: vec3) -> (vec3, number),
    normalizewithlength: @checked (a: vec3) -> (vec3, number),
    subpos: @checked (a: vec3, b: vec3) -> vec3,
    offsetpos: @checked (p: vec3, d: vec3) -> vec3,
    lerpposition: @checked (a: vec3, b: vec3, t: number) -> vec3,
    isvalid: @checked (a: vec3) -> boolean,
}

declare quat: {
    new: @checked (x: number?, y: number?, z: number?, w: number?) -> quat,
    conjugate: @checked (q: quat) -> quat,
    nlerp: @checked (a: quat, b: quat, alpha: number) -> quat,
    normalize: @checked (q: quat) -> quat,
    invmul: @checked (a: quat, b: quat) -> quat,
    dot: @checked (a: quat, b: quat) -> number,
    negate: @checked (q: quat) -> quat,
    fromaxisangle: @checked (axis: vec3, radians: number) -> quat,
    axisangle: @checked (q: quat) -> (vec3, number),
    angle: @checked (q: quat) -> number,
    twistangle: @checked (q: quat) -> number,
    swingangle: @checked (q: quat) -> number,
    betweenunitvectors: @checked (a: vec3, b: vec3) -> quat,
    isvalid: @checked (q: quat) -> boolean,
    isnormalized: @checked (q: quat) -> boolean,
    frommatrix: @checked (m: mat3) -> quat,
}

declare transform: {
    new: @checked (p: vec3?, q: quat?) -> transform,
    inverse: @checked (t: transform) -> transform,
    point: @checked (t: transform, v: vec3) -> vec3,
    direction: @checked (t: transform, v: vec3) -> vec3,
    invpoint: @checked (t: transform, v: vec3) -> vec3,
    invdirection: @checked (t: transform, v: vec3) -> vec3,
    invmul: @checked (a: transform, b: transform) -> transform,
    makeworld: @checked (t: transform) -> transform,
    mulworld: @checked (a: transform, b: transform) -> transform,
    invmulworld: @checked (a: transform, b: transform) -> transform,
    transformworldpoint: @checked (t: transform, p: vec3) -> vec3,
    invtransformworldpoint: @checked (t: transform, p: vec3) -> vec3,
    torelative: @checked (t: transform, base: vec3) -> transform,
    isvalid: @checked (t: transform) -> boolean,
}

declare vec2: {
    new: @checked (x: number?, y: number?) -> vec2,
}

declare mat3: {
    new: @checked (cx: vec3?, cy: vec3?, cz: vec3?) -> mat3,
    transpose: @checked (m: mat3) -> mat3,
    inverse: @checked (m: mat3) -> mat3,
    inversewide: @checked (m: mat3) -> mat3,
    inverset: @checked (m: mat3) -> mat3,
    solve: @checked (m: mat3, v: vec3) -> vec3,
    solvewide: @checked (m: mat3, v: vec3) -> vec3,
    abs: @checked (m: mat3) -> mat3,
    det: @checked (m: mat3) -> number,
    fromquat: @checked (q: quat) -> mat3,
    isvalid: @checked (m: mat3) -> boolean,
}

declare plane: {
    new: @checked (normal: vec3, offset: number?) -> plane,
    isvalid: @checked (p: plane) -> boolean,
}

declare aabb: {
    new: @checked (lower: vec3, upper: vec3) -> aabb,
    frompoints: @checked (points: {vec3}, radius: number?) -> aabb,
    contains: @checked (a: aabb, b: aabb) -> boolean,
    area: @checked (a: aabb) -> number,
    center: @checked (a: aabb) -> vec3,
    extents: @checked (a: aabb) -> vec3,
    union: @checked (a: aabb, b: aabb) -> aabb,
    inflate: @checked (a: aabb, e: number) -> aabb,
    overlaps: @checked (a: aabb, b: aabb) -> boolean,
    transform: @checked (t: transform, a: aabb) -> aabb,
    fromcenterextents: @checked (t: transform, center: vec3, extents: vec3) -> aabb,
    closestpoint: @checked (point: vec3, a: aabb) -> vec3,
    isvalid: @checked (a: aabb) -> boolean,
}

declare fix30: {
    new: @checked (x: number) -> fix30,
    tonumber: @checked (x: fix30) -> number,
    normalizecomponent: @checked (x: number, length: number) -> fix30,
}

)BUILTIN_SRC";

[[maybe_unused]] static const char* const kBuiltinDefinitionIntegerSrc = R"BUILTIN_SRC(

declare integer: {
    create: @checked (x: number) -> integer,
    tonumber: @checked (x: integer) -> number,
    neg: @checked (value: integer) -> integer,
    add: @checked (x: integer, y: integer) -> integer,
    sub: @checked (x: integer, y: integer) -> integer,
    mul: @checked (x: integer, y: integer) -> integer,
    div: @checked (x: integer, y: integer) -> integer,
    rem: @checked (x: integer, y: integer) -> integer,
    idiv: @checked (x: integer, y: integer) -> integer,
    mod: @checked (x: integer, y: integer) -> integer,
    udiv: @checked (x: integer, y: integer) -> integer,
    urem: @checked (x: integer, y: integer) -> integer,
    min: @checked (integer, ...integer) -> integer,
    max: @checked (integer, ...integer) -> integer,
    band: @checked (...integer) -> integer,
    bor: @checked (...integer) -> integer,
    bnot: @checked (x: integer) -> integer,
    bxor: @checked (...integer) -> integer,
    lt: @checked (x: integer, y: integer) -> boolean,
    le: @checked (x: integer, y: integer) -> boolean,
    ult: @checked (x: integer, y: integer) -> boolean,
    ule: @checked (x: integer, y: integer) -> boolean,
    gt: @checked (x: integer, y: integer) -> boolean,
    ge: @checked (x: integer, y: integer) -> boolean,
    ugt: @checked (x: integer, y: integer) -> boolean,
    uge: @checked (x: integer, y: integer) -> boolean,
    lshift: @checked (x: integer, numBitPositions: integer) -> integer,
    rshift: @checked (x: integer, numBitPositions: integer) -> integer,
    arshift: @checked (x: integer, numBitPositions: integer) -> integer,
    lrotate: @checked (x: integer, numBitPositions: integer) -> integer,
    rrotate: @checked (x: integer, numBitPositions: integer) -> integer,
    extract: @checked (value: integer, bitPosition: integer, numBits: integer?) -> integer,
    replace: @checked (value: integer, replacement: integer, bitPosition: integer, numBits: integer?) -> integer,
    clamp: @checked (value: integer, min: integer, max: integer) -> integer,
    btest: @checked (...integer) -> boolean,
    countrz: @checked (x: integer) -> integer,
    countlz: @checked (x: integer) -> integer,
    bswap: @checked (x: integer) -> integer,
    fromstring: @checked (str: string, base: number?) -> integer,
    minsigned: integer,
    maxsigned: integer
}

)BUILTIN_SRC";

static const char* kBuiltinDefinitionClassSrc = R"CLASS_SRC(
declare class: {
    isinstance: @checked (o: unknown, c: class) -> boolean,
    classof: @checked (o: unknown) -> class?
}
)CLASS_SRC";

std::string getBuiltinDefinitionSource()
{
    std::string result = kBuiltinDefinitionBaseSrc;

    result += kBuiltinDefinitionBit32Src;
    result += kBuiltinDefinitionMathSrc;
    // moo: the os library is deleted from the VM (DETERMINISM.md "Status", 9), so the analyzer must not declare it
    // (DESIGN.md "Analyzer").

    if (FFlag::DebugLuauCoroutineFinallyAnalysis)
        result += kBuiltinDefinitionCoroutineSrc;
    else
        result += kBuiltinDefinitionCoroutineSrc_DEPRECATED;

    if (FFlag::DebugLuauExactTableTypes)
        result += kBuiltinDefinitionTableSrc_EXACT_TABLES;
    else
        result += kBuiltinDefinitionTableSrc;
    result += kBuiltinDefinitionDebugSrc;
    result += kBuiltinDefinitionUtf8Src;
    // moo: number is Q48.16 and there is no integer type or library (DESIGN.md "Analyzer"), so the buffer
    // definition never mentions integer, whatever the upstream flags say.
    result += kBuiltinDefinitionBufferSrc_NOINTEGER;
    // moo: vec3, quat and transform (docs/history/layer3b/moo-v2-l3b-analyzer-types.md)
    result += kBuiltinDefinitionMooValueTypesSrc;

    if (FFlag::DebugLuauUserDefinedClasses && FFlag::LuauAllowGlobalDeclarationToBeCalledClass)
    {
        result += kBuiltinDefinitionClassSrc;
    }

    return result;
}

// TODO: split into separate tagged unions when the new solver can appropriately handle that.
[[maybe_unused]] static constexpr const char* kBuiltinDefinitionTypeMethodSrc = R"BUILTIN_SRC(

export type type = {
    tag: "nil" | "unknown" | "never" | "any" | "boolean" | "number" | "integer" | "string" | "buffer" | "thread" |
         "singleton" | "negation" | "union" | "intersection" | "table" | "function" | "extern" | "generic",

    is: (self: type, arg: string) -> boolean,
    issubtypeof: (self: type, arg: type) -> boolean,

    -- for singleton type
    value: (self: type) -> (string | boolean | nil),

    -- for negation type
    inner: (self: type) -> type,

    -- for union and intersection types
    components: (self: type) -> {type},

    -- for table type
    setproperty: (self: type, key: type, value: type?) -> (),
    setreadproperty: (self: type, key: type, value: type?) -> (),
    setwriteproperty: (self: type, key: type, value: type?) -> (),
    readproperty: (self: type, key: type) -> type?,
    writeproperty: (self: type, key: type) -> type?,
    properties: (self: type) -> { [type]: { read: type?, write: type? } },
    setindexer: (self: type, index: type, result: type) -> (),
    setreadindexer: (self: type, index: type, result: type) -> (),
    setwriteindexer: (self: type, index: type, result: type) -> (),
    indexer: (self: type) -> { index: type, readresult: type, writeresult: type }?,
    readindexer: (self: type) -> { index: type, result: type }?,
    writeindexer: (self: type) -> { index: type, result: type }?,
    setmetatable: (self: type, arg: type) -> (),
    metatable: (self: type) -> type?,

    -- for function type
    setparameters: (self: type, head: {type}?, tail: type?) -> (),
    parameters: (self: type) -> { head: {type}?, tail: type? },
    setreturns: (self: type, head: {type}?, tail: type? ) -> (),
    returns: (self: type) -> { head: {type}?, tail: type? },
    setgenerics: (self: type, {type}?) -> (),
    generics: (self: type) -> {type},

    -- for class type
    -- 'properties', 'metatable', 'indexer', 'readindexer' and 'writeindexer' are shared with table type
    readparent: (self: type) -> type?,
    writeparent: (self: type) -> type?,

    -- for generic type
    name: (self: type) -> string?,
    ispack: (self: type) -> boolean,
}

)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionTypeMethodSrc_NOINTEGER = R"BUILTIN_SRC(

export type type = {
    tag: "nil" | "unknown" | "never" | "any" | "boolean" | "number" | "string" | "buffer" | "thread" |
         "singleton" | "negation" | "union" | "intersection" | "table" | "function" | "extern" | "generic",

    is: (self: type, arg: string) -> boolean,
    issubtypeof: (self: type, arg: type) -> boolean,

    -- for singleton type
    value: (self: type) -> (string | boolean | nil),

    -- for negation type
    inner: (self: type) -> type,

    -- for union and intersection types
    components: (self: type) -> {type},

    -- for table type
    setproperty: (self: type, key: type, value: type?) -> (),
    setreadproperty: (self: type, key: type, value: type?) -> (),
    setwriteproperty: (self: type, key: type, value: type?) -> (),
    readproperty: (self: type, key: type) -> type?,
    writeproperty: (self: type, key: type) -> type?,
    properties: (self: type) -> { [type]: { read: type?, write: type? } },
    setindexer: (self: type, index: type, result: type) -> (),
    setreadindexer: (self: type, index: type, result: type) -> (),
    setwriteindexer: (self: type, index: type, result: type) -> (),
    indexer: (self: type) -> { index: type, readresult: type, writeresult: type }?,
    readindexer: (self: type) -> { index: type, result: type }?,
    writeindexer: (self: type) -> { index: type, result: type }?,
    setmetatable: (self: type, arg: type) -> (),
    metatable: (self: type) -> type?,

    -- for function type
    setparameters: (self: type, head: {type}?, tail: type?) -> (),
    parameters: (self: type) -> { head: {type}?, tail: type? },
    setreturns: (self: type, head: {type}?, tail: type? ) -> (),
    returns: (self: type) -> { head: {type}?, tail: type? },
    setgenerics: (self: type, {type}?) -> (),
    generics: (self: type) -> {type},

    -- for class type
    -- 'properties', 'metatable', 'indexer', 'readindexer' and 'writeindexer' are shared with table type
    readparent: (self: type) -> type?,
    writeparent: (self: type) -> type?,

    -- for generic type
    name: (self: type) -> string?,
    ispack: (self: type) -> boolean,
}

)BUILTIN_SRC";

[[maybe_unused]] static constexpr const char* kBuiltinDefinitionTypesLibSrc = R"BUILTIN_SRC(

declare types: {
    unknown: type,
    never: type,
    any: type,
    boolean: type,
    number: type,
    string: type,
    thread: type,
    buffer: type,
    integer: type,

    singleton: @checked (arg: string | boolean | nil) -> type,
    optional: @checked (arg: type) -> type,
    generic: @checked (name: string, ispack: boolean?) -> type,
    negationof: @checked (arg: type) -> type,
    unionof: @checked (...type) -> type,
    intersectionof: @checked (...type) -> type,
    newtable: @checked (props: {[type]: type} | {[type]: { read: type?, write: type? } }?, indexer: { index: type, readresult: type, writeresult: type }?, metatable: type?) -> type,
    newfunction: @checked (parameters: { head: {type}?, tail: type? }?, returns: { head: {type}?, tail: type? }?, generics: {type}?) -> type,
    copy: @checked (arg: type) -> type,
}
)BUILTIN_SRC";

static constexpr const char* kBuiltinDefinitionTypesLibSrc_NOINTEGER = R"BUILTIN_SRC(

declare types: {
    unknown: type,
    never: type,
    any: type,
    boolean: type,
    number: type,
    string: type,
    thread: type,
    buffer: type,

    singleton: @checked (arg: string | boolean | nil) -> type,
    optional: @checked (arg: type) -> type,
    generic: @checked (name: string, ispack: boolean?) -> type,
    negationof: @checked (arg: type) -> type,
    unionof: @checked (...type) -> type,
    intersectionof: @checked (...type) -> type,
    newtable: @checked (props: {[type]: type} | {[type]: { read: type?, write: type? } }?, indexer: { index: type, readresult: type, writeresult: type }?, metatable: type?) -> type,
    newfunction: @checked (parameters: { head: {type}?, tail: type? }?, returns: { head: {type}?, tail: type? }?, generics: {type}?) -> type,
    copy: @checked (arg: type) -> type,
}
)BUILTIN_SRC";

std::string getTypeFunctionDefinitionSource()
{
    std::string result;

    // moo: there is no integer type (DESIGN.md "Analyzer"), so always use the definition without it.
    result += kBuiltinDefinitionTypeMethodSrc_NOINTEGER;
    result += kBuiltinDefinitionTypesLibSrc_NOINTEGER;

    return result;
}

} // namespace Luau
