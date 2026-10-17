-- Adversarial: allocation churn with weak keys, weak values and both, userdata with
-- destructors in five size classes, and no explicit collection: what survives at each
-- checkpoint, and when each destructor runs (it logs), depend only on GC pacing.
local wk = setmetatable({}, {__mode = "k"})
local wv = setmetatable({}, {__mode = "v"})
local wkv = setmetatable({}, {__mode = "kv"})
local id = setmetatable({}, {__mode = "k"})
local hold = {}
local sizes = {8, 40, 200, 1000, 3000}
local n = 0
for round = 1, 200 do
    for i = 1, 50 do
        n += 1
        local k
        if i % 5 == 0 then k = newud(sizes[n % 5 + 1])
        elseif i % 5 == 1 then k = newudi(sizes[(n * 3) % 5 + 1])
        elseif i % 5 == 2 then k = function() return n end
        else k = {n, string.rep("y", n % 13)} end
        id[k] = n
        wk[k] = {n}
        wv[n] = k
        wkv[k] = k
        if math.random(1, 9) == 1 then hold[#hold + 1] = k end
    end
    while #hold > 60 do table.remove(hold, math.random(1, #hold)) end
    if round % 20 == 0 then
        local line = {}
        for k in pairs(wk) do line[#line + 1] = id[k] end
        local c = 0
        for _ in pairs(wv) do c += 1 end
        for _ in pairs(wkv) do c += 1 end
        log("round", round, gcinfo(), c, table.concat(line, " "))
    end
end
