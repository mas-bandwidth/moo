-- Adversarial: generators, nested resumes, yields across pcall, errors inside coroutines,
-- close, wrap, status, and allocation inside each, with object-keyed bookkeeping.
local registry = {}
local function gen(n)
    return coroutine.create(function(a)
        local acc = {}
        for i = 1, n do
            acc[#acc + 1] = {i, a}
            local ok, v = pcall(function() return coroutine.yield(i * a) end)
            a = (v or a) + 1
        end
        return #acc
    end)
end
local cos = {}
for i = 1, 40 do
    local co = gen(i % 7 + 1)
    cos[#cos + 1] = co
    registry[co] = i
end
local out = {}
local alive = #cos
local step = 0
while alive > 0 do
    alive = 0
    for _, co in cos do
        if coroutine.status(co) ~= "dead" then
            step += 1
            local ok, v = coroutine.resume(co, step % 11)
            out[#out + 1] = registry[co] .. ":" .. tostring(ok) .. ":" .. tostring(v)
            if coroutine.status(co) ~= "dead" then alive += 1 end
        end
    end
end
log("generators", step, table.concat(out, ","))
local errs = {}
for i = 1, 20 do
    local co = coroutine.create(function()
        coroutine.yield(i)
        if i % 3 == 0 then error({code = i}) end
        if i % 3 == 1 then error("plain " .. i) end
        return i
    end)
    coroutine.resume(co)
    local ok, e = coroutine.resume(co)
    errs[#errs + 1] = tostring(ok) .. ":" .. (type(e) == "table" and ("code" .. e.code) or tostring(e))
end
log("errors", table.concat(errs, ","))
local w = coroutine.wrap(function()
    for i = 1, 5 do coroutine.yield(coroutine.isyieldable(), coroutine.running() ~= nil, i) end
end)
local wl = {}
for i = 1, 5 do local a, b, c = w() wl[#wl + 1] = tostring(a) .. tostring(b) .. c end
log("wrap", table.concat(wl, ","))
local closers = {}
for i = 1, 10 do
    local co = coroutine.create(function() coroutine.yield() end)
    coroutine.resume(co)
    closers[#closers + 1] = tostring(coroutine.close(co)) .. coroutine.status(co)
end
log("close", table.concat(closers, ","))
local depth = 0
local function nest(k)
    if k == 0 then return coroutine.yield("bottom") end
    local co = coroutine.create(nest)
    local ok, v = coroutine.resume(co, k - 1)
    depth += 1
    return v
end
log("nest", nest(30), depth)
