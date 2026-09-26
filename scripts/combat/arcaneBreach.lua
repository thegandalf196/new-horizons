local Base = require("combat/combatScript")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- Arcane Breach marks are inert state. Their effect is consumed by the battle damage pipeline;
-- this registered trigger intentionally handles no combat events.

return Script
