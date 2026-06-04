-- json: non-finite numbers encode as null instead of throwing.
local json = require("json")

-- nan and both infinities all become null in the output.
print(json.encode({ nan = 0 / 0, pos = 1 / 0, neg = -1 / 0 }))

-- a mixed array keeps finite values and nulls out the rest.
print(json.encode({ 1, 1 / 0, 2.5, 0 / 0 }))
