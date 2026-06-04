-- json: security checks for the parser and encoder boundaries.
-- the json security doc tracks cases by CWE class rather than numbered ids, so each case cites its class.
-- covered classes: recursion bounds (CWE-674), resource exhaustion (CWE-400), invalid encoding handling
-- (CWE-176), control-char injection (CWE-74/CWE-116), numeric range and precision (CWE-681/CWE-190),
-- malformed input rejection (CWE-20), and exception safety (CWE-248).
local json = require("json")

-- CWE-674: deeply nested input is rejected before the parser can overflow the stack.
assert(not pcall(json.decode, string.rep("[", 5000) .. string.rep("]", 5000)), "deep nesting is rejected")

-- CWE-674: a shallow document at a safe depth still parses, so the cap is a bound and not a blanket reject.
assert(pcall(json.decode, string.rep("[", 100) .. string.rep("]", 100)), "shallow nesting parses")

-- CWE-674: deeply nested encode is bounded and does not crash the process.
local deep, cur = {}, nil
cur = deep
for _ = 1, 5000 do cur.n = {}; cur = cur.n end
assert(pcall(json.encode, deep), "deep encode is bounded, not a crash")

-- CWE-400: a huge but bounded flat array parses without exhausting resources.
local big = "[" .. string.rep("1,", 50000) .. "1]"
local ok, arr = pcall(json.decode, big)
assert(ok and #arr == 50001, "huge flat array is handled")

-- CWE-400: a large string value round-trips with its length intact.
local huge = json.decode(json.encode(string.rep("x", 100000)))
assert(#huge == 100000, "large string value round-trips")

-- CWE-74 / CWE-116: control characters are escaped on encode and raw control bytes are rejected on decode.
assert(json.encode("a\1b") == '"a\\u0001b"', "control char is escaped on encode")
assert(not pcall(json.decode, '"a' .. string.char(1) .. 'b"'), "raw control byte in a string is rejected")

-- CWE-176: invalid utf-8 in a value is replaced on encode rather than crossing the boundary as a throw.
assert(type(json.encode("a" .. string.char(0xff, 0xfe, 0x80) .. "b")) == "string", "invalid utf-8 does not crash the encoder")

-- CWE-176: an embedded NUL survives a binary round-trip with its length preserved.
local nul = json.decode(json.encode("a\0b"))
assert(#nul == 3, "embedded NUL is preserved")

-- CWE-681 / CWE-190: a number past the lua_Integer range decodes to a float, never a wrapped integer.
local past = json.decode("123456789012345678901234567890")
assert(math.type(past) == "float", "out-of-range integer becomes a float")

-- CWE-400: an absurd exponent is rejected instead of being expanded.
assert(not pcall(json.decode, "1e1000000"), "huge exponent literal is rejected")

-- CWE-20: duplicate keys resolve deterministically with the last value winning.
assert(json.decode('{"a":1,"a":2}').a == 2, "duplicate keys are last-wins")

-- CWE-20: malformed, truncated, and empty inputs are all rejected via pcall.
assert(not pcall(json.decode, ""), "empty input is rejected")
assert(not pcall(json.decode, '{"a":'), "truncated object is rejected")
assert(not pcall(json.decode, "[1,2,"), "truncated array is rejected")
assert(not pcall(json.decode, "{}x"), "trailing garbage is rejected")

print("json security ok")
