local Base = require("combat/combatScript")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- This registered trigger makes the spell-effect marker visible as an active
-- spell. End-of-turn damage and spread are resolved by BattleFlowProcessor.

return Script
