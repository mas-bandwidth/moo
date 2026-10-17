-- Adversarial: tables keyed by every collectable kind, with deletes during traversal,
-- regrowth, rehash, and pairs order logged after each phase.
local kinds = {}
local name = {}
local t = {}
for i = 1, 300 do
    local k
    local m = i % 6
    if m == 0 then k = {}
    elseif m == 1 then k = function() return i end
    elseif m == 2 then k = coroutine.create(function() end)
    elseif m == 3 then k = buffer.create(i % 17 + 1)
    elseif m == 4 then k = newproxy()
    else k = newud(16) end
    name[k] = i
    t[k] = i * 2
    kinds[#kinds + 1] = k
end
local function dump(tag, tab)
    local out = {}
    for k, v in pairs(tab) do out[#out + 1] = name[k] .. "=" .. tostring(v) end
    log(tag, #out, table.concat(out, ","))
end
dump("insert", t)
local n = 0
for k in pairs(t) do
    n += 1
    if n % 4 ~= 0 then t[k] = nil end
end
dump("delete", t)
for i = 1, #kinds, 3 do t[kinds[i]] = -i end
dump("reinsert", t)
local nested = {}
for i = 1, 50 do nested[kinds[i]] = { [kinds[i + 1]] = kinds[i + 2] } end
local out = {}
for k, v in pairs(nested) do
    for k2, v2 in pairs(v) do out[#out + 1] = name[k] .. ">" .. name[k2] .. ">" .. name[v2] end
end
log("nested", table.concat(out, ","))
log("tostring", tostring(kinds[1]), tostring(kinds[2]), tostring(kinds[3]), tostring(kinds[4]), tostring(kinds[5]), tostring(kinds[6]))
