-- The compiler-determinism corpus (tests/moo/compile_determinism.test.cpp): a module with many exported
-- functions, values and an upvalue, so the export table's order shows if it ever follows addresses
-- (docs/DETERMINISM.md, 25). Compiled only; never run.

export local version = 3
export local name = "exports"
local hidden = 7

export function alpha(x) return x + 1 end
export function bravo(x) return x * 2 end
export function charlie(x) return alpha(x) + bravo(x) end
export function delta() return version end
export function echo(...) return ... end
export function foxtrot(t) return #t end
export function golf(a, b) return a < b end
export function hotel() return hidden end
export function india(s) return `{name}:{s}` end
export function juliet() return function() return charlie(hidden) end end
export function kilo(n)
    local sum = 0
    for i = 1, n do
        sum += i
    end
    return sum
end
export function lima(t)
    return { a = t, b = t, c = { t, t } }
end

export local mike = { alpha = alpha, bravo = bravo }
export local november, oscar = 1.5, "oscar"
