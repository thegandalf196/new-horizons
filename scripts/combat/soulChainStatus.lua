local Base = require("combat/combatScript")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- This combat-event trigger exists so Soul Chain is represented as a normal,
-- serializable, dispellable spell status. Damage echoes are resolved once by
-- the authoritative battle-damage packet hook.

return Script
