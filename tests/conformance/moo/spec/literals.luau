-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: Number literals: the parser gives the compiler the exact raw Q48.16 value, and an integer literal is
-- refused (docs/DESIGN.md "Literals", "integer").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
    -- moo: number literals reach the compiler as the exact raw Q48.16 value (docs/history/layer3/moo-l3-ast-raw-literals.md;
    -- docs/DESIGN.md "Layer 3 as built", "Literals"), so a literal that needs more than 53 significant bits compiles to
    -- exactly that raw constant. The largest value and three more literals above 2^37 all round to math.huge, the largest
    -- value (docs/DESIGN.md "math library"), and tostring reads the exact raw value back.
    assert(tostring(140737488355327.99998474121094) == tostring(math.huge), "the largest literal")
    assert(tostring(140737488355327.9999847412109375) == tostring(math.huge), "the exact largest value")
    assert(tostring(140737488355327.99998474121092) == tostring(math.huge), "a second literal above 2^37")
    assert(tostring(140737488355327.9999847412109) == tostring(math.huge), "a third literal above 2^37")

    -- 2^46 - 1 + one unit needs 62 significant bits; the double would round it, the raw value does not.
    assert(moo_test.raw(70368744177663.00001525878906) == "4611686018427322369", "a raw value above 2^53")
end

return "OK"
