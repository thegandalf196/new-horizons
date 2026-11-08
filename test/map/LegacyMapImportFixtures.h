/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <string>
#include <vector>
#include "../../lib/scripting/nativeMap/LegacyHotaPrelude.h"

namespace legacyMapFixtures
{
inline std::string generatedProgram(const std::string & handlers, const std::vector<std::string> & resourceOrder)
{
 std::string source = "-- generated from MigrationFixture.h3m HotA event system\nlocal Map = {}\nlocal RESOURCES = {";
 for(size_t i = 0; i < resourceOrder.size(); ++i)
  source += (i ? ", " : "") + std::string("\"") + resourceOrder[i] + "\"";
 source += "}\n" + std::string(scripting::nativeMap::legacy::HELPERS);
 source += "local Vars = {[7] = \"counter\"}\n";
 return source + handlers + "\nreturn Map\n";
}

inline const std::vector<std::string> & rejectedStatements()
{
 static const std::vector<std::string> rejected{
		R"lua(server:giveExperience(town, 1))lua",
		R"lua(server:grantCreaturesToHire(hero, 0, 1))lua",
		R"lua(server:grantSpellbook(hero, server:random(1, 2)))lua",
		R"lua(server:showMessage(ENUM.PlayerColor.blue, {append={"text"}}, toComponents({})))lua",
		R"lua(server:showMessage(player, {append={"text"}}, toComponents({}), server:random(1, 2)))lua",
		R"lua(Map:heroEvents_2(game, server, town, hero))lua",
		R"lua(Map:heroEvents_2(game, server, object, hero, server:random(1, 2)))lua",
		R"lua(Map:townEvents_2(game, server, object, hero))lua",
		R"lua(Map.heroEvents_2(game, server, object, hero))lua",
		R"lua(server.grantSpellbook(hero))lua",
		R"lua(server:grantSpell(hero, LIBRARY.getSpellByName("core:magicArrow")))lua",
		R"lua(server:giveExperience(hero, game:getDifficulty(server:random(1, 2))))lua",
		R"lua(server:giveExperience(hero, game:getCalendar(server:random(1, 2)):getCurrentDay()))lua",
		R"lua(server:giveExperience(hero, game:getCalendar():getCurrentDay(server:random(1, 2))))lua",
		R"lua(server:giveExperience(hero, game:getCurrentDay()))lua",
		R"lua(server:giveExperience(hero, game.getDifficulty()))lua",
		R"lua(server:giveExperience(hero, compareDifficulty(server, 0)))lua",
		R"lua(server:giveExperience(hero, compareDifficulty(game, 0, server:random(1, 2))))lua",
		R"lua(server:giveExperience(hero, math:floor(5 / 2)))lua",
		R"lua(server:grantSpell(hero, LIBRARY:getSpellByName("core:magicArrow", server:random(1, 2))))lua",
		R"lua(if hero:hasArtifact(LIBRARY:getArtifactByName("core:spellbook"), server:random(1, 2)) then end)lua",
		R"lua(if playerDefeated(server, resolvePlayer(-1, player)) then end)lua",
		R"lua(if playerDefeated(game, resolvePlayer(-1, player), server:random(1, 2)) then end)lua",
		R"lua(if playerDefeated(game, resolvePlayer(-1, hero)) then end)lua",
		R"lua(if playerDefeated(game, -1) then end)lua",
		R"lua(if (player == resolvePlayer(-1, player)) then end)lua",
		R"lua(if (town:getOwner(server:random(1, 2)) == ENUM.PlayerColor.neutral) then end)lua",
		R"lua(if (hero:getSecondarySkill(LIBRARY:getSecondarySkillByName("core:wisdom"), server:random(1, 2)) >= 1) then end)lua",
		R"lua(server:showMessage(player, {append={"first","second"}}, toComponents({})))lua",
		R"lua(server:showMessage(player, {append={"text"}, replace={"other"}}, toComponents({})))lua",
		R"lua(server:showMessage(player, {append={"text"}}, toComponents({}, server:random(1, 2))))lua",
		R"lua(server:showMessage(player, {append={"text"}}, {{2,6,1}}))lua",
		R"lua(setQuestHint(server, player, object, {append={"hint"}}, toComponents({}), server:random(0, 1)))lua",
		R"lua(setQuestHint(server, player, object, {append={"hint"}}, toComponents({}), 1))lua",
		R"lua(server:showQuestion{text={append={"text"}}, images={}, onYes=function() end, onNo=function() end, extra=server:random(1, 2)})lua",
		R"lua(grantResources(server, player, {0,0,0,0,0,0,0,server:random(1, 2)}))lua",
		R"lua(grantResources(server, player, {0,0,0,0,0,0,0,extra=server:random(1, 2)}))lua",
		R"lua(for __i = 1, 2 do server:setMapVariable(Vars[7], __i, server:random(1, 2)) end)lua",
	};
 return rejected;
}
inline constexpr auto QUESTION_HANDLERS = R"legacy(
function Map:heroEvents_1(game, server, object, hero)
 local player = hero:getOwner()
 server:showQuestion{
  text = {append = {"question"}}, images = {},
  onYes = function() grantResources(server, player, {0,0,0,0,0,0,100}) end,
  onNo = function() grantResources(server, player, {0,0,0,0,0,0,5}) end,
 }
 grantResources(server, player, {0,0,0,0,0,1,0})
end
)legacy";
inline constexpr auto NESTED_HANDLERS = R"legacy(
function Map:heroEvents_1(game, server, object, hero)
 local player = hero:getOwner()
 for __i = 1, server:random(2, 3) do
  server:setMapVariable(Vars[7], __i)
  if (not false and ((game:getDifficulty() >= 0) or (game:getCalendar():getCurrentDay() < 2))) then
   server:setMapVariable(Vars[7], (game:getMapVariable(Vars[7]) + math.floor((-5) / 2)))
  elseif hero:hasArtifact(LIBRARY:getArtifactByName("core:spellbook")) then
   server:grantMorale(hero, -2)
  else
   server:grantLuck(hero, 3)
  end
 end
 server:showRewardsMessage(player, {append={"reward"}}, function()
  server:grantScroll(hero, LIBRARY:getSpellByName("core:magicArrow"))
 end)
 Map:heroEvents_2(game, server, object, hero)
end
function Map:heroEvents_2(game, server, object, hero)
 local player = hero:getOwner()
 server:showMessage(player, {append={"message"}}, toComponents({{2,6,(5 % (-2))}}))
end
)legacy";
inline constexpr auto COMPLETE_HANDLERS = R"lua(
function Map:heroEvents_1(game, server, object, hero)
 local player = hero:getOwner()
 server:grantSpellbook(hero)
 server:giveExperience(hero, game:getDifficulty())
 server:giveExperience(hero, compareDifficulty(game, 0))
 server:giveExperience(hero, game:getCalendar():getCurrentDay())
 if (player == ENUM.PlayerColor.red) then
  server:showMessage(player, {append={"text"}}, toComponents({{2,6,server:random(1,2)}}))
 end
 if playerDefeated(game, resolvePlayer(-1, player)) then end
 setQuestHint(server, player, object, {append={"hint"}}, toComponents({}), false)
 Map:questEvents_2(game, server, object, hero)
end
function Map:questEvents_2(game, server, object, hero)
 local player = hero:getOwner()
 registerQuest(game, server, player, object, {
  proposal={append={"proposal"}}, progression={append={"progression"}},
  completion={append={"completion"}}, hint={append={"hint"}},
  condition=function() return true end,
  reward=function() server:finishQuestOrRemoveObject(object) end,
 })
end
function Map:townEvents_1(game, server, town)
 local player = town:getOwner()
 server:grantCreaturesToHire(town, 0, 7)
 server:constructBuilding(town, 0)
 Map:townEvents_2(game, server, town)
end
function Map:townEvents_2(game, server, town)
 local player = town:getOwner()
 if (town:getOwner() == ENUM.PlayerColor.neutral) then end
end
function Map:playerEvents_1(game, server, player)
 grantResources(server, player, {0,0,0,0,0,0,1})
 Map:playerEvents_2(game, server, player)
end
function Map:playerEvents_2(game, server, player)
end
)lua";
}
