local Base = require("spells/spellEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- Purify's action center and selected effect groups are validated and applied by
-- the authoritative C++ battle path. This effect keeps the spell on the ordinary
-- location-targeting spell pipeline without invoking Dispel's broader behavior.
function Script:applicableTarget(mechanics, problem, target)
	return #target == 1 and target[1].hex ~= nil
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	return aimPoint
end

function Script:apply(mechanics, server, target)
	return
end

return Script
