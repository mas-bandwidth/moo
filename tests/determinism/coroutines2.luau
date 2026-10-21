-- Adversarial coroutine programs (layer 5, docs/DETERMINISM.md 21). One section per
-- coroutine transition and one per refused transition; each refusal is checked for its
-- exact error text, and the result is logged so the determinism suite pins it.
local function capture(f)
    local r = {pcall(f)}
    local parts = {}
    for i = 1, #r do parts[i] = tostring(r[i]) end
    return table.concat(parts, ":")
end

local out = {}

-- 1. generator: resume -> running -> yield -> suspended -> ... -> return -> dead
do
    local co = coroutine.create(function(a)
        local x = a
        for i = 1, 3 do
            local r = coroutine.yield(x + i)
            x = r
        end
        return x * 10
    end)
    local rec = {}
    for i = 1, 4 do
        local ok, v = coroutine.resume(co, i * 3)
        rec[#rec + 1] = tostring(ok) .. ":" .. tostring(v) .. ":" .. coroutine.status(co)
    end
    out[#out + 1] = "generator " .. table.concat(rec, " ")
end

-- 2. yield across pcall: the yield crosses the pcall boundary inside a coroutine.
do
    local co = coroutine.create(function()
        local ok, v = pcall(function() return coroutine.yield("across") end)
        return ok, v
    end)
    local a, b, c = coroutine.resume(co)
    out[#out + 1] = "yield-pcall " .. tostring(a) .. ":" .. tostring(b) .. ":" .. tostring(c)
end

-- 3. errors thrown from nested resumes: A resumes B, B errors, A catches it and continues.
do
    local co = coroutine.create(function()
        local B = coroutine.create(function() error("inner failure") end)
        local ok, e = coroutine.resume(B)
        return ok, tostring(e)
    end)
    local a, b, c = coroutine.resume(co)
    out[#out + 1] = "nested-error " .. tostring(a) .. ":" .. tostring(b) .. ":" .. tostring(c)
end

-- 4. deep nesting: a chain of coroutines, each resuming the next; the deepest yields and the chain unwinds.
do
    local deep = 0
    local function nest(k)
        if k == 0 then return coroutine.yield("bottom") end
        local inner = coroutine.create(nest)
        local ok, v = coroutine.resume(inner, k - 1)
        deep += 1
        return v
    end
    local top = coroutine.create(nest)
    local a, b, c = coroutine.resume(top, 12)
    out[#out + 1] = "nest " .. tostring(a) .. ":" .. tostring(b) .. ":" .. tostring(c) .. ":" .. deep
end

-- 5. close inside pcall, on suspended and on errored coroutines.
do
    local suspended = coroutine.create(function() coroutine.yield() end)
    coroutine.resume(suspended)
    local a = capture(function() return coroutine.close(suspended) end)
    local errored = coroutine.create(function() error("boom") end)
    coroutine.resume(errored)
    local b = capture(function() return coroutine.close(errored) end)
    out[#out + 1] = "close-pcall " .. a .. " " .. coroutine.status(suspended) .. " " .. b .. " " .. coroutine.status(errored)
end

-- 6. close of a dead coroutine (finished normally) is allowed and returns true.
do
    local dead = coroutine.create(function() end)
    coroutine.resume(dead)
    local a = capture(function() return coroutine.close(dead) end)
    out[#out + 1] = "close-dead " .. a .. ":" .. coroutine.status(dead)
end

-- 7. refused: close the running coroutine (itself).
do
    local co
    co = coroutine.create(function()
        return capture(function() return coroutine.close(co) end)
    end)
    local a, b = coroutine.resume(co)
    out[#out + 1] = "refuse-close-running " .. tostring(a) .. ":" .. b
end

-- 8. refused: resume a dead coroutine.
do
    local dead = coroutine.create(function() end)
    coroutine.resume(dead)
    local a = capture(function() return coroutine.resume(dead) end)
    out[#out + 1] = "refuse-resume-dead " .. a
end

-- 9. refused: resume the running coroutine (itself).
do
    local co
    co = coroutine.create(function()
        return capture(function() return coroutine.resume(co) end)
    end)
    local a, b = coroutine.resume(co)
    out[#out + 1] = "refuse-resume-running " .. tostring(a) .. ":" .. b
end

-- 10. wrap: normal use, then calling a dead wrapper is a thrown error.
do
    local w = coroutine.wrap(function()
        coroutine.yield("w1")
        return "w2"
    end)
    local first = w()
    local second = w()
    local third = capture(function() return w() end)
    out[#out + 1] = "wrap " .. tostring(first) .. " " .. tostring(second) .. " " .. third
end

-- 11. isyieldable is true inside a coroutine, and running() returns the thread.
do
    local co = coroutine.create(function()
        return coroutine.isyieldable(), coroutine.running() ~= nil
    end)
    local a, b, c = coroutine.resume(co)
    out[#out + 1] = "isyieldable " .. tostring(a) .. ":" .. tostring(b) .. ":" .. tostring(c)
end

-- 12. refused: yield inside a metamethod (__index) crosses a C-call boundary.
do
    local mt = {__index = function() return coroutine.yield("meta") end}
    local t = setmetatable({}, mt)
    local co = coroutine.create(function()
        return capture(function() return t.x end)
    end)
    local a, b = coroutine.resume(co)
    out[#out + 1] = "refuse-yield-metamethod " .. tostring(a) .. ":" .. b
end

-- 13. refused: yield inside a table.sort comparator crosses a C-call boundary.
do
    local co = coroutine.create(function()
        return capture(function()
            table.sort({3, 1, 2}, function(a, b)
                coroutine.yield(a)
                return a < b
            end)
        end)
    end)
    local a, b = coroutine.resume(co)
    out[#out + 1] = "refuse-yield-sort " .. tostring(a) .. ":" .. b
end

log(table.concat(out, "\n"))
