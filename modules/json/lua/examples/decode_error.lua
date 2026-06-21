-- json: decode raises on invalid input, so guard it with pcall.
local json = require("json")

local ok, err = pcall(json.decode, "{not json")
print(ok, err)

local value = json.decode('{"valid":true}')
print(value.valid)
