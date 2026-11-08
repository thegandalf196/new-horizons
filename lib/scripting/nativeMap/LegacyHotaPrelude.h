/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
// Historical generated helper text is a format signature, never executed.
// Only this known implementation is accepted by the one-time legacy importer.
namespace scripting::nativeMap::legacy
{
inline constexpr auto HELPERS = R"signature(
-- HotA player sentinels: negative values mean "the event's own player".
local function resolvePlayer(player, eventPlayer)
	if player < 0 then return eventPlayer end
	return player
end

-- Converts HotA image triples {kind, subtype, amount} to engine Component tables.
local function toComponents(images)
	local result = {}
	for _, image in ipairs(images) do
		result[#result + 1] = {type = image[1], subType = image[2], value = image[3]}
	end
	return result
end

-- Adds a whole resource set to a player; the amounts follow the engine resource order.
local function grantResources(server, player, amounts)
	for index, key in ipairs(RESOURCES) do
		local amount = amounts[index]
		if amount and amount ~= 0 then server:giveResource(player, LIBRARY:getResourceByName(key), amount) end
	end
end

-- HotA object-identity predicates. The engine only offers the lookups; the comparison lives here.
local function heroOwner(game, heroType, player)
	local hero = game:getHeroByType(heroType)
	return hero ~= nil and hero:getOwner() == player
end

local function playerOwnsTown(game, player, objectName)
	local town = game:getObjectByName(objectName)
	return town ~= nil and town:getOwner() == player
end

local function playerDefeated(game, player)
	return game:getPlayerStatus(player) == ENUM.PlayerStatus.loser
end

local function playerStartingFaction(game, player, faction)
	return game:getPlayerFaction(player) == faction
end

local function playerDestroyedObject(game, player, objectName)
	local target = game:getObjectByName(objectName)
	return target ~= nil and game:playerDestroyedObject(player, target)
end

-- Negative if the current difficulty is easier than the reference, zero if equal, positive if harder.
local function compareDifficulty(game, reference)
	return game:getDifficulty() - reference
end

-- HotA scripted quest (a seer hut / quest guard whose condition and reward are script-driven
-- instead of the engine's built-in mission types). Evaluated fresh on every visit: shows the
-- completion message and runs the reward once the condition holds, otherwise the proposal (first
-- time) or progression (repeat visits) message, tracked per player via markQuestProposed.
local function registerQuest(game, server, player, object, opts)
	if opts.condition() then
		server:showMessage(player, opts.completion, {})
		opts.reward()
	elseif game:wasQuestProposed(object, player) then
		server:showMessage(player, opts.progression, {})
	else
		server:markQuestProposed(object, player)
		server:setQuestHintText(object, opts.hint)
		server:addToQuestLog(object, player)
		server:showMessage(player, opts.proposal, {})
	end
end

-- Updates a scripted quest's quest-log / hover hint text, optionally adding it to the player's
-- quest log (HotA's SET_QUEST_HINT action).
-- TODO: `images` is ignored - a quest-log hint has no engine-side component list yet.
local function setQuestHint(server, player, object, text, images, showInLog)
	server:setQuestHintText(object, text)
	if showInLog then server:addToQuestLog(object, player) end
end

)signature";
}