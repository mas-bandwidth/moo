-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
--
-- moo-specific expected behaviour. Replaces upstream tests/conformance/datetime.luau, which
-- tested os.time, os.date, os.difftime and os.clock.
--
-- Why: the os library is removed in moo (docs/DETERMINISM.md, 9). os.clock and os.time read
-- the machine's clocks and os.date formats with the host's time zone and locale, so no program
-- that calls them is deterministic. Time is an input: a host that wants it passes it in.

assert(os == nil)
assert(rawget(_G, "os") == nil)

-- a script that reaches for the clock fails at the call, by name
local ok, err = pcall(function() return os.clock() end)
assert(not ok)
assert(string.find(err, "attempt to index nil with 'clock'", 1, true))

return "OK"
