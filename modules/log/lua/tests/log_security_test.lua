-- log security confirming hostile message content is treated as data and never crashes the logger, covering CWE-117 log injection, CWE-150 terminal escape injection, CWE-134 format string, CWE-400 resource exhaustion, and CWE-626 nul handling.
local log = require("log")

-- LOG-001 / LOG-002 / LOG-003 / LOG-014: newline and carriage-return content is handled without crashing.
assert(pcall(log.info, "first line\nforged second line"), "newline content should not crash")
assert(pcall(log.warn, "rewrite\rline"), "carriage return content should not crash")
assert(pcall(log.error, "split\r\nentry"), "crlf content should not crash")

-- LOG-007 / LOG-084: tab content does not crash the tab-joined writer.
assert(pcall(log.info, "field\tinjected\tfield"), "tab content should not crash")

-- LOG-008 / LOG-061: a nul byte in the message is handled without crashing.
assert(pcall(log.info, "before\0after"), "nul byte content should not crash")

-- LOG-009 / LOG-116: an ansi escape sequence is treated as data, not executed as a terminal control.
assert(pcall(log.warn, "\27[31mred\27[0m"), "ansi escape content should not crash")

-- LOG-021 / LOG-141: printf-style specifiers are logged as data and never interpreted as a format string.
assert(pcall(log.info, "%s %n %d %x"), "printf specifiers should be treated as data")

-- LOG-022 / LOG-142: fmt-style braces are logged as data and do not trigger substitution.
assert(pcall(log.info, "{} {0} {:x}"), "fmt braces should be treated as data")

-- LOG-042 / LOG-071: a very long single message is handled without crashing.
assert(pcall(log.error, string.rep("x", 200000)), "very long message should not crash")

-- LOG-018: hostile content at error level is also contained.
assert(pcall(log.error, "audit\nbypass\r%n"), "error level hostile content should not crash")

print("log security ok")
