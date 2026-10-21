-- moo: layer 5 coroutine transitions and refusals (DETERMINISM.md 21). MooL3Scripts runs this in a
-- fresh VM with the moo_test helpers; it returns "OK" (docs/layer3/README.md).
local function expect(cond, what)
    assert(cond, what)
end

-- 1. generator: resume -> running -> yield -> suspended, until return makes it dead
do
    local co = coroutine.create(function(a)
        local x = a
        for i = 1, 3 do x = coroutine.yield(x + i) end
        return x * 10
    end)
    local s1 = {coroutine.resume(co, 2)}
    expect(s1[1] and s1[2] == 3 and coroutine.status(co) == "suspended", "generator yield")
    local s2 = {coroutine.resume(co, 5)}
    expect(s2[1] and s2[2] == 7, "generator second yield")
    local s3 = {coroutine.resume(co, 0)}
    expect(s3[1] and s3[2] == 3, "generator third yield")
    local s4 = {coroutine.resume(co, 9)}
    expect(s4[1] and s4[2] == 90 and coroutine.status(co) == "dead", "generator return")
end

-- 2. a yield crosses a pcall boundary inside a coroutine
do
    local co = coroutine.create(function()
        local ok, v = pcall(function() return coroutine.yield("across") end)
        return tostring(ok) .. ":" .. tostring(v)
    end)
    local r1 = {coroutine.resume(co)}
    expect(r1[1] and r1[2] == "across", "yield across pcall")
    local r2 = {coroutine.resume(co, "back")}
    expect(r2[1] and r2[2] == "true:back", "yield resumes across pcall")
end

-- 3. deep nesting: a chain of coroutines unwinds to the deepest yield
do
    local function nest(k)
        if k == 0 then return coroutine.yield("bottom") end
        local inner = coroutine.create(nest)
        local ok, v = coroutine.resume(inner, k - 1)
        return v
    end
    local ok, v = coroutine.resume(coroutine.create(nest), 6)
    expect(ok and v == "bottom", "deep nesting unwinds")
end

-- 4. an error thrown by a nested resume is caught by the parent
do
    local co = coroutine.create(function()
        local B = coroutine.create(function() error("inner failure") end)
        local ok, e = coroutine.resume(B)
        return tostring(ok) .. ":" .. tostring(e):match("inner failure")
    end)
    local ok, r = coroutine.resume(co)
    expect(ok and r == "false:inner failure", "nested resume error caught")
end

-- 5. close inside pcall on suspended, errored and dead coroutines
do
    local suspended = coroutine.create(function() coroutine.yield() end)
    coroutine.resume(suspended)
    local ok, c1 = pcall(coroutine.close, suspended)
    expect(ok and c1 == true and coroutine.status(suspended) == "dead", "close suspended")

    local errored = coroutine.create(function() error("boom") end)
    coroutine.resume(errored)
    local ok2, r, emsg = pcall(coroutine.close, errored)
    expect(ok2 and r == false and tostring(emsg):match("boom"), "close errored")

    local dead = coroutine.create(function() end)
    coroutine.resume(dead)
    local ok3, d1 = pcall(coroutine.close, dead)
    expect(ok3 and d1 == true, "close dead")
end

-- 6. refused transitions, each checked for its exact error text
local function refusal(f)
    local results = {pcall(f)}
    local parts = {}
    for i = 1, #results do parts[i] = tostring(results[i]) end
    return table.concat(parts, ":")
end

do
    local co
    co = coroutine.create(function()
        return refusal(function() return coroutine.close(co) end)
    end)
    local ok, r = coroutine.resume(co)
    expect(ok and r:match("cannot close running coroutine"), "refuse close running")
end

do
    local dead = coroutine.create(function() end)
    coroutine.resume(dead)
    local co = coroutine.create(function()
        return refusal(function() return coroutine.resume(dead) end)
    end)
    local ok, r = coroutine.resume(co)
    expect(ok and r:match("cannot resume dead coroutine"), "refuse resume dead")
end

do
    local co
    co = coroutine.create(function()
        return refusal(function() return coroutine.resume(co) end)
    end)
    local ok, r = coroutine.resume(co)
    expect(ok and r:match("cannot resume running coroutine"), "refuse resume running")
end

do
    local w = coroutine.wrap(function() coroutine.yield("w") return "d" end)
    expect(w() == "w" and w() == "d", "wrap normal")
    local ok, e = pcall(w)
    expect(not ok and tostring(e):match("cannot resume dead coroutine"), "wrap dead")
end

do
    local t = setmetatable({}, {__index = function() return coroutine.yield("meta") end})
    local co = coroutine.create(function()
        local ok, e = pcall(function() return t.x end)
        return tostring(ok) .. ":" .. tostring(e)
    end)
    local ok, r = coroutine.resume(co)
    expect(ok and r == "false:attempt to yield across metamethod/C-call boundary", "refuse metamethod yield")
end

do
    local co = coroutine.create(function()
        local ok, e = pcall(function()
            table.sort({3, 1, 2}, function(a, b)
                coroutine.yield(a)
                return a < b
            end)
        end)
        return tostring(ok) .. ":" .. tostring(e)
    end)
    local ok, r = coroutine.resume(co)
    expect(ok and r == "false:attempt to yield across metamethod/C-call boundary", "refuse sort compar yield")
end

return "OK"
