-- assert-based test for validate: required/optional, types, min/max on values and lengths, pattern, enum, default substitution, nested tables, and arrays of a rule, checking both the ok flag and the specific error paths.
local dir = arg[0]:match("^(.*)[/\\]") or "."
package.path = ("%s/../../?.lua;%s/../../?/init.lua;"):format(dir, dir) .. package.path

local validate = require("validate")

local schema = {
    name = validate.string{ min = 1, max = 10 },
    age = validate.integer{ min = 0, max = 120 },
    active = validate.boolean{},
    score = validate.number{ min = 0 },
    role = validate.string{ enum = { "admin", "user" }, default = "user" },
    email = validate.string{ pattern = "^[%w._%%+-]+@[%w.-]+%.%a+$", required = false },
    tags = validate.array(validate.string{ min = 1 }, { required = false, max = 3 }),
    profile = validate.table({
        city = validate.string{ min = 1 },
    }, { required = false }),
}

-- a fully valid payload passes with no errors.
local ok, errors = validate.check(schema, {
    name = "alice",
    age = 30,
    active = true,
    score = 9.5,
    email = "alice@example.com",
    tags = { "a", "b" },
    profile = { city = "Lisbon" },
})
assert(ok == true, "valid payload passes")
assert(errors == nil, "no errors on success")

-- a default fills in a missing optional field, so role being absent is fine.
ok = validate.check(schema, { name = "bob", age = 20, active = false, score = 1 })
assert(ok == true, "default supplies missing field")

-- missing required fields are reported by path.
ok, errors = validate.check(schema, {})
assert(not ok, "missing required fields fail")
assert(errors.name == "is required", "name required")
assert(errors.age == "is required", "age required")
assert(errors.active == "is required", "active required")
assert(errors.score == "is required", "score required")
assert(errors.role == nil, "field with default is not required")

-- type mismatches.
ok, errors = validate.check(schema, { name = 5, age = "x", active = "yes", score = "n" })
assert(errors.name == "must be a string", "string type")
assert(errors.age == "must be a number", "number type")
assert(errors.active == "must be a boolean", "boolean type")

-- integer rejects a fractional number.
ok, errors = validate.check(schema, { name = "a", age = 1.5, active = true, score = 1 })
assert(errors.age == "must be an integer", "integer rejects fraction")

-- min/max on a string length and a number value.
ok, errors = validate.check(schema, { name = "", age = 200, active = true, score = -1 })
assert(errors.name == "must be at least 1", "string min length")
assert(errors.age == "must be at most 120", "number max value")
assert(errors.score == "must be at least 0", "number min value")

ok, errors = validate.check(schema, { name = "way-too-long-name", age = 5, active = true, score = 1 })
assert(errors.name == "must be at most 10", "string max length")

-- enum.
ok, errors = validate.check(schema, { name = "a", age = 5, active = true, score = 1, role = "root" })
assert(errors.role == "must be one of the allowed values", "enum membership")

-- pattern on an optional field.
ok, errors = validate.check(schema, { name = "a", age = 5, active = true, score = 1, email = "nope" })
assert(errors.email == "has an invalid format", "pattern mismatch")

-- array element validation and array length cap.
ok, errors = validate.check(schema, { name = "a", age = 5, active = true, score = 1, tags = { "ok", "" } })
assert(errors["tags[2]"] == "must be at least 1", "array element rule by index")

ok, errors = validate.check(schema, { name = "a", age = 5, active = true, score = 1, tags = { "a", "b", "c", "d" } })
assert(errors.tags == "must be at most 3", "array length cap")

-- nested table errors carry a dotted path.
ok, errors = validate.check(schema, { name = "a", age = 5, active = true, score = 1, profile = { city = "" } })
assert(errors["profile.city"] == "must be at least 1", "nested field path")

-- a non-table top-level value fails cleanly.
ok, errors = validate.check(schema, "not a table")
assert(not ok and errors._ ~= nil, "non-table data rejected")

print("validate integration ok")
