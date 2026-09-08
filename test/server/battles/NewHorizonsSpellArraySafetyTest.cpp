/*
 * NewHorizonsSpellArraySafetyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/json/JsonRandom.h"
#include "../../../lib/spells/CSpell.h"
#include <stdexcept>
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/rewardable/Info.h"
#include "../../../lib/rewardable/Configuration.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/gameState/CGameState.h"

class NewHorizonsSpellArraySafetyTest : public HeroCommandFixture {};

TEST_F(NewHorizonsSpellArraySafetyTest, ScalarAbsenceIsNoneButArrayAbsenceCannotWeakenRequirements)
{
	startGame();
	GameRandomizer randomizer(*gameState());
	JsonRandom json(gameState().get(), randomizer);
	JsonNode missing("core:missingArraySafetySpell");
	missing.setModScope(ModScope::scopeBuiltin());
	EXPECT_EQ(json.loadSpell(missing, {}), SpellID(SpellID::NONE));
	JsonNode array;
	array.Vector().push_back(missing);
	EXPECT_THROW(json.loadSpells(array, {}), std::runtime_error);
	array.Vector().clear();
	EXPECT_TRUE(json.loadSpells(array, {}).empty());
	const SpellID arrow(SpellID::MAGIC_ARROW);
	gameState()->getMap().allowedSpells.erase(arrow);
	JsonNode named("core:magicArrow");
	named.setModScope(ModScope::scopeBuiltin());
	EXPECT_EQ(json.loadSpell(named, {}), arrow) << "Named legacy map-ban override remains intentional";
}

TEST_F(NewHorizonsSpellArraySafetyTest, ActualRewardAndLimiterArrayConsumersFailClosed)
{
	startGame();
	GameRandomizer randomizer(*gameState());
	for(const bool limiter : {false, true})
		for(const std::string key : (limiter ? std::vector<std::string>{"spells", "scrolls", "canLearnSpells"}
			: std::vector<std::string>{"spells", "scrolls", "takenScrolls"}))
		{
			SCOPED_TRACE(key);
			JsonNode data;
			data["rewards"].Vector().resize(1);
			auto & entry = data["rewards"].Vector().front();
			auto & fields = limiter ? entry["limiter"] : entry;
			fields[key].Vector().push_back(JsonNode("core:missingArraySafetySpell"));
			data.setModScope(ModScope::scopeBuiltin());
			Rewardable::Info info;
			info.init(data, "spellArraySafety");
			Rewardable::Configuration configured;
			EXPECT_THROW(info.configureObject(configured, randomizer, gameState().get()), std::runtime_error);
			EXPECT_TRUE(configured.info.empty());
		}
}

TEST_F(NewHorizonsSpellArraySafetyTest, UnavailableVariableFailsBeforePublicationAndValidVariableResolvesText)
{
	startGame();
	GameRandomizer randomizer(*gameState());
	JsonNode data;
	data["variables"]["spell"]["chosen"].String() = "core:missingArraySafetySpell";
	data["rewards"].Vector().resize(1);
	data["rewards"].Vector().front()["message"].Vector().push_back(JsonNode("%s"));
	data.setModScope(ModScope::scopeBuiltin());
	Rewardable::Info info;
	info.init(data, "spellVariableSafety");
	Rewardable::Configuration configured;
	EXPECT_THROW(info.configureObject(configured, randomizer, gameState().get()), std::runtime_error);
	EXPECT_FALSE(configured.getVariable("spell", "chosen").has_value());
	EXPECT_TRUE(configured.info.empty());
	data["variables"]["spell"]["chosen"].String() = "core:magicArrow";
	info.init(data, "spellVariableSafety");
	ASSERT_NO_THROW(info.configureObject(configured, randomizer, gameState().get()));
	ASSERT_TRUE(configured.getVariable("spell", "chosen").has_value());
	EXPECT_EQ(*configured.getVariable("spell", "chosen"), SpellID(SpellID::MAGIC_ARROW).getNum());
	ASSERT_EQ(configured.info.size(), 1u);
	EXPECT_EQ(configured.info.front().message.toString(LIBRARY->staticTexts()), SpellID(SpellID::MAGIC_ARROW).toSpell()->getNameTranslated());
}

class NewHorizonsGuildArraySafetyTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->allowedSpells.erase(SpellID(SpellID::MAGIC_ARROW));
		for(auto * town : map->getObjects<CGTownInstance>())
		{
			town->obligatorySpells = {SpellID(SpellID::NONE), SpellID(SpellID::MAGIC_ARROW)};
			town->possibleSpells = {SpellID(SpellID::NONE)};
		}
	}
};

TEST_F(NewHorizonsGuildArraySafetyTest, InvalidMandatoryAndPossibleIdsCannotIndexBucketsButLegacyMandatoryBanOverrideRemains)
{
	startGame(true);
	ASSERT_FALSE(gameState()->getMap().getAllTowns().empty());
	for(const auto townId : gameState()->getMap().getAllTowns())
	{
		const auto * town = gameState()->getTown(townId);
		bool found = false;
		for(const auto & level : town->spells)
		{
			EXPECT_FALSE(vstd::contains(level, SpellID(SpellID::NONE)));
			found |= vstd::contains(level, SpellID(SpellID::MAGIC_ARROW));
		}
		EXPECT_TRUE(found);
	}
}
