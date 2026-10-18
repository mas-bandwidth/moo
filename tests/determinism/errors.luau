-- Adversarial: every error kind the VM raises, captured with pcall and debug.traceback, every
-- string logged, so DETERMINISM.md 20 (error messages and tracebacks are deterministic) pins the
-- exact bytes. One section per kind; each log line is the tag, the value's type, the error string
-- and its traceback.
local out = {}
local function catch(tag, f)
    local ok, e = pcall(f)
    local s
    if ok then
        s = "ok"
    else
        local tb = debug.traceback(tostring(e), 2)
        s = type(e) .. "|" .. tostring(e) .. "|tb|" .. tb
    end
    out[#out + 1] = tag .. "=" .. s
end

-- arithmetic on nil
catch("arith nil", function() return 1 + nil end)
catch("arith nil2", function() return nil - 1 end)
catch("arith str", function() return 1 + "x" end)

-- call errors
local nv
catch("call nil", function() return nv() end)
catch("call number", function() local x = 3 return x() end)

-- index errors
catch("index nil", function() local t = nil return t.x end)
catch("index nilnum", function() local t = nil return t[1] end)
catch("index nilkey", function() local t = {} t[nil] = 1 end)

-- moo named errors
catch("div zero", function() return 1 / 0 end)
catch("for step zero", function() for i = 1, 10, 0 do end end)

-- metamethod errors
local meta = setmetatable({}, {__index = function() error("meta boom") end})
catch("mm index err", function() return meta.x end)
catch("mm len wrong", function() return #setmetatable({}, {__len = function() return "x" end}) end)
catch("mm index chain", function()
    local t = {}
    for i = 1, 200 do t = setmetatable({}, {__index = t}) end
    return t.x
end)
catch("mm newindex chain", function()
    local t = {}
    for i = 1, 200 do t = setmetatable({}, {__newindex = t}) end
    t.x = 1
end)

-- concat and compare
catch("concat table", function() return {} .. "x" end)
catch("compare num str", function() return 1 < "x" end)
catch("compare table", function() local a, b = {}, {} return a < b end)

-- coroutine errors
catch("co resume dead", function()
    local co = coroutine.create(function() end)
    coroutine.resume(co)
    return coroutine.resume(co)
end)
catch("co close running", function()
    local co
    co = coroutine.create(function() return coroutine.close(co) end)
    return coroutine.resume(co)
end)
catch("co yield mm", function()
    local t = setmetatable({}, {__index = function() return coroutine.yield() end})
    local co = coroutine.create(function() return t.x end)
    return coroutine.resume(co)
end)

-- stack overflow at a fixed depth
catch("stack overflow", function()
    local function rec(n) return rec(n + 1) end
    return rec(0)
end)

log("errors", table.concat(out, "\n"))
