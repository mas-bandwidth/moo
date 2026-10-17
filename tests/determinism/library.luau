-- The rest of the observable surface without math transcendentals: random with the host seed,
-- sort with a comparator, # on tables with holes, string and buffer, error messages and
-- tracebacks, tostring of every kind.
local r = {}
for i = 1, 50 do r[#r + 1] = math.random(1, 1000) end
log("random", table.concat(r, ","))
math.randomseed(7)
log("reseed", math.random(1, 1e6), math.random(1, 1e6))
local a = {}
for i = 1, 500 do a[i] = (i * 7919) % 1009 end
table.sort(a, function(x, y) return x % 37 < y % 37 or (x % 37 == y % 37 and x < y) end)
log("sort", table.concat(a, ",", 1, 60))
local holes = {}
for i = 1, 64 do holes[i] = i end
for i = 3, 64, 5 do holes[i] = nil end
log("border", #holes)
holes[100] = 1
log("border2", #holes)
local s = string.rep("abc", 40) .. string.format("%d-%5.2f-%x-%s", 42, 1.5, 255, "z")
log("string", #s, string.sub(s, 100), s:upper():sub(1, 9), string.find(s, "cab", 50, true))
local b = buffer.create(32)
buffer.writeu32(b, 0, 0xdeadbeef)
buffer.writei16(b, 4, -2)
buffer.writestring(b, 6, "moo")
log("buffer", buffer.readu32(b, 0), buffer.readi16(b, 4), buffer.readstring(b, 6, 3), buffer.len(b))
local ok, e = pcall(function() local t = {} return t.x.y end)
log("error", ok, e)
ok, e = pcall(error, setmetatable({}, {__tostring = function() return "custom" end}))
log("errorobj", ok, tostring(e))
log("traceback", debug.traceback("tb", 1))
local values = { {}, function() end, coroutine.create(function() end), buffer.create(1), newproxy(), newud(8), "s", 1, true }
local kinds = {}
for _, v in values do kinds[#kinds + 1] = typeof(v) .. "=" .. tostring(v) end
log("tostring", table.concat(kinds, " "))
