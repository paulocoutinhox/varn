-- xml: security checks for the parser and encoder boundaries.
-- covered classes: external entity / XXE prevention (CWE-611), entity expansion (CWE-776),
-- recursion bounds (CWE-674), output injection (CWE-91), invalid encoding handling (CWE-176),
-- and exception safety (CWE-248).
local xml = require("xml")

-- XML-001 / XML-101 / XML-200: an XXE payload never reads a local file and the entity is not expanded.
local xxe = '<?xml version="1.0"?><!DOCTYPE foo [<!ENTITY xxe SYSTEM "file:///etc/passwd">]><root>&xxe;</root>'
local ok, res = pcall(xml.decode, xxe)
assert(ok, "xxe payload is parsed safely without crashing")
assert(not (res.text or ""):find("root:"), "xxe must not expose /etc/passwd contents")
assert(not (res.text or ""):find("/bin"), "xxe must not expose any file contents")

-- XML-007 / XML-116 / XML-245: a billion-laughs payload does not expand and does not crash.
local bomb = '<?xml version="1.0"?><!DOCTYPE lolz [<!ENTITY lol "lol">' ..
    '<!ENTITY a "&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;">]><lolz>&a;</lolz>'
local okb, resb = pcall(xml.decode, bomb)
assert(okb, "entity-expansion payload is parsed without a memory blowup")
assert(#(resb.text or "") < 100, "general entity is not expanded into the document")

-- XML-099: a bare DOCTYPE without entities is handled safely.
assert(pcall(xml.decode, '<?xml version="1.0"?><!DOCTYPE r><r>x</r>'), "bare DOCTYPE is handled safely")

-- XML-032 / XML-079 / XML-087: deeply nested elements are bounded and never crash the process.
assert(pcall(xml.decode, string.rep("<a>", 600) .. string.rep("</a>", 600)), "moderately deep nesting is handled")
assert(pcall(xml.decode, string.rep("<a>", 20000) .. string.rep("</a>", 20000)), "very deep nesting is depth-capped, not a crash")

-- XML-046 / XML-161 / XML-264: special characters in text are escaped, not emitted as raw markup.
local injected = xml.encode({ name = "n", text = "</n><script>x</script>" })
assert(not injected:find("<script>", 1, true), "text is escaped so it cannot inject markup")
assert(xml.decode(injected).text == "</n><script>x</script>", "escaped text round-trips to the original")

-- XML-048 / XML-163 / XML-266: an invalid element name is sanitized so it cannot break the document.
assert(xml.encode({ name = "bad name!", text = "x" }):find("<bad_name_", 1, true), "invalid name is sanitized")

-- XML-037 / XML-066: invalid utf-8 in a node value does not crash the encoder.
assert(type(xml.encode({ name = "n", text = "a" .. string.char(0xff, 0xfe) .. "b" })) == "string",
    "invalid utf-8 in text does not crash the encoder")

-- XML-058: encode rejects a non-table node through the binding rather than crashing.
assert(not pcall(xml.encode, "string"), "encode rejects a non-table node")

-- XML-023 / XML-024 / XML-040 / XML-041 / XML-042: malformed, unclosed, and empty inputs are rejected via pcall.
assert(not pcall(xml.decode, "<unclosed>"), "unclosed tag is rejected")
assert(not pcall(xml.decode, "<a></b>"), "mismatched tags are rejected")
assert(not pcall(xml.decode, "not xml at all"), "non-xml input is rejected")
assert(not pcall(xml.decode, ""), "empty input is rejected")

print("xml security ok")
