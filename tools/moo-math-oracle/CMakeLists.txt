# This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
#
# moo-math-oracle: the offline generator of tests/moo/vectors/math-oracle.bin (README.md). A standalone project, never
# part of moo's build: it needs CORE-MATH's files at the pinned commit, and moo's tests read only the frozen file.
# README.md, "Regenerate", has the commands.
cmake_minimum_required(VERSION 3.16)
project(moo-math-oracle C)

# the CORE-MATH commit the frozen file was generated from, and the SHA-256 of every CORE-MATH file the generator
# compiles at that commit. The configure step refuses a source tree whose files differ, so a clone and a download of
# the files at the pin build the same generator. Moving the pin regenerates the file (README.md, "Regenerate").
set(MOO_CORE_MATH_PIN "dae5dd8c0d5bd2691b45de28c9edbe12f067c3d3")
set(MOO_CORE_MATH_FILES
    acos/acos.c ecd6173837214367f0140c038c615a4f37d27795c0cc74e16bdaa59f7df8cff1
    asin/asin.c 08c4d31ad429e4aac3cc1adf9dbc6e31d59b6ff997626fbfcd3b60ebe2cbffe8
    atan2/atan2.c 404b0db90e536f8248101544d0fcad9141fd76b2a6cc3d189dd80dbfc865bd91
    atan2/tint.h 41264d54812a8ce2b3f8ca4c8c033094df7cbf2d214da1c7555783328171b921
    atan/atan.c 2d62a4d780f233fd0460b8e6076880fcb9d2f55280ead5799180983b6b77c863
    cos/cos.c 04bffbc8b483c886f78f2c9ab3f2a1ca318dda78ab637a2983a4a9d82374e9d9
    cosh/cosh.c aa9e80b3fe3fa4aa0b3b2abf11a9fe184f0170c5faa042ad80dcd4698417842b
    exp/exp.c 55ad39a6610e0f46794be6b70a1ee4d76b10dd4f64d47b61c4e918d711e3041d
    log10/dint.h 3bdfe6175e6d107506badbaf3f6cd1005165bee4e601827d1f1da2560f84d264
    log10/log10.c 32eae639bee4d0f77721c7abc9322a58eb05d6303be49386961b6399fe4de066
    log2/log2.c 2097e97c8955de82c0c5df22997ba13617e148ef691c2a86f01ece4cc2310ace
    log/dint.h 0da80a15d311bb1e550035f98edc2266ef31828cd423aec3a2e4507d2dad5e0e
    log/log.c 922af9ae4b4bd009bf13238ac8a20c1be25d1fee912b0c35dbfd4962c1572497
    pow/dint.h c62a9788d872c314c743a821602d6819e5c13113b70f6d4a5d0076695dc413ef
    pow/pow.c b14c65a1d1367f49a0d3e00e65370c3e9dd0fd004fe00e30cdad2a9887954562
    pow/pow.h 8e28930b514f1f439011cb201c738c6b8940f321f03f4679362e7f6b7893fd6a
    pow/qint.h 32de9c10388056e2294339e3302c3695cdf4abc87ca790ad48917d9794bfb6e4
    sinh/sinh.c bafb6b0e42c69607ee36d00b5d72724c4662881e1152cdc0adc02c4bdbf3edb6
    sin/sin.c cd772a06d75d1bb8bb4374847065acfab28d5a06c7fb81be3b36127f65d8b1c2
    tanh/tanh.c e89441340424ea7d137269ce4626b9c1426c37fade09c0853c0ffcca606fb111
    tan/tan.c dd662b7af88a86d85e01b88c57f9df4b6019ddfd02120ca381b5ba42283987ae)

set(CORE_MATH_DIR "" CACHE PATH "CORE-MATH's src/binary64 files at the pin (a clone, or README.md's download)")
list(LENGTH MOO_CORE_MATH_FILES MOO_CORE_MATH_LENGTH)
math(EXPR MOO_CORE_MATH_LAST "${MOO_CORE_MATH_LENGTH} - 1")
foreach(i RANGE 0 ${MOO_CORE_MATH_LAST} 2)
    math(EXPR j "${i} + 1")
    list(GET MOO_CORE_MATH_FILES ${i} path)
    list(GET MOO_CORE_MATH_FILES ${j} expected)
    set(file "${CORE_MATH_DIR}/src/binary64/${path}")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "missing ${file}: set CORE_MATH_DIR to CORE-MATH at ${MOO_CORE_MATH_PIN} (README.md)")
    endif()
    file(SHA256 "${file}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${file} is not CORE-MATH ${MOO_CORE_MATH_PIN}'s (sha256 ${actual}, pinned ${expected})")
    endif()
endforeach()

set(MOO_CORE_MATH_FUNCTIONS sin cos tan asin acos atan atan2 exp log log10 log2 sinh cosh tanh pow)

add_executable(moo-math-oracle gen.c)
foreach(f ${MOO_CORE_MATH_FUNCTIONS})
    add_library(moo-cr-${f} OBJECT "${CORE_MATH_DIR}/src/binary64/${f}/${f}.c")
    target_include_directories(moo-cr-${f} PRIVATE "${CORE_MATH_DIR}/src/binary64/${f}")
    target_compile_options(moo-cr-${f} PRIVATE -O2 -fno-fast-math -ffp-contract=off)
    target_sources(moo-math-oracle PRIVATE $<TARGET_OBJECTS:moo-cr-${f}>)
endforeach()

# 65536 seeded draws per sampled function keeps the file under 8 MB (README.md, "Size")
target_compile_definitions(moo-math-oracle PRIVATE MOO_CORE_MATH_COMMIT="${MOO_CORE_MATH_PIN}" MOO_ORACLE_SAMPLES=65536)
target_compile_options(moo-math-oracle PRIVATE -O2 -std=c11 -Wall -Wextra -Werror -fno-fast-math -fno-builtin -ffp-contract=off)
target_link_libraries(moo-math-oracle PRIVATE m)
