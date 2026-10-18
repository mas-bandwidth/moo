# moo-l4-format-roundtrip

Layer 4's formatting check: every formatted value round-trips. This card is a check, not a change to the
formatter: the formatter and parser (moo_format.h, moo_parse.h, built in layer 3) were verified against the
layer 4 contract, and no design decision changed.

## Decision

Keep the layer 3 formatter and parser as written. The round-trip guarantee, the exact `%.16f` expansion and the
half-to-even `%e`/`%g` rules all hold for the checked pool (2^24 seeded raw values plus the edges below), so no
`docs/DESIGN.md` or `docs/DETERMINISM.md` sentence needs to change.

## What changed

- `tests/moo/moo-l4-format-roundtrip.test.cpp`: `TEST_CASE("FormatRoundtrip")` (doctest, built into `Moo.Test`).
  For every raw value `n` in the pool it checks:
  - `tonumber(tostring(n)) == n` bit for bit, through the VM (`vmTonumberRoundtrip`):
    `moo_numtostr` (Common/include/moo_format.h:63) then `moo_numparse` (Common/include/moo_parse.h);
  - `string.format("%.16f", n)` equals the exact expansion, computed independently in `exactExpansion`
    from `raw * 10^16 / 2^16` (integer part `raw >> 16`, 16 fraction digits `(raw & 0xffff) * 5^16`);
  - `%e` and `%g` at precisions 0-17 match an independent reference formatter (`refFormatted` / `refRound` /
    `refBodyE` / `refBodyF` / `refBodyG`) that rounds half to even on the exact digit string.
  The pool is `2^18` raw values by default; `MOO_FORMAT_N` (an environment variable read at run time) raises it to
  `2^24` for the nightly. The edge set is zero, ±1 raw, ±`MOO_NUMBER_MAX` (`INT64_MAX`), every power of two raw
  (2^0..2^62, both signs), and values with 1..16 significant fraction digits (`fractionDigitEdge`:
  `f = 2^(16-d)` has `v2(f) = 16-d`, so the decimal fraction has exactly `d` significant digits).
- `tests/conformance/moo/l3/moo-l4-format-roundtrip.luau`: runs through the VM (via `MooL3Scripts`) and pins
  the rules on concrete raw values: round-trip, `tostring` shortest forms, `%.16f` exact expansions, and the
  `%e`/`%g` half-to-even ties on exact values.

## Sentences that must change

None. The check confirms the design as written:

- `docs/DESIGN.md` "Layer 3 as built", "Formatting and tonumber are exact": "`tostring` prints the shortest
  decimal that reads back to exactly the same value ... `string.format`'s `%f`, `%e`, `%g`, `%E` and `%G`
  follow C's flags, width and precision on the exact decimal expansion, rounding half to even, with no libc."
- `docs/DESIGN.md` "Number formatting": "`tostring(x)` prints the **shortest decimal that reads back to exactly
  the same raw value** (...) `tonumber(tostring(x)) == x` bit for bit, always" and "every Q48.16 value has a
  finite decimal expansion of at most 16 fraction digits, which `string.format("%.16f", x)` prints."
- `docs/DESIGN.md` "string.format": "the exact decimal expansion of the value is rounded to the requested
  precision, ties to even on the exact value, by moo's integer formatter."
- `docs/DETERMINISM.md` section 8 fix: one deterministic integer formatter and parser, no libc, used by
  `tostring` and `string.format`.

These hold as tested; no edit is required.
