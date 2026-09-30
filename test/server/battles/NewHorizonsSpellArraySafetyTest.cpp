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
#include "../../../lib/GameSettings.h"
#include "../../../lib/json/JsonRandom.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include <stdexcept>
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/rewardable/Info.h"
#include "../../../lib/rewardable/Configuration.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include <optional>

class NewHorizonsSpellArraySafetyTest : public HeroCommandFixture
{
protected:
	std::optional<JsonNode> magicRulesOverride;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		const auto magicRules = magicRulesOverride.value_or(JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
	}
};

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

TEST_F(NewHorizonsSpellArraySafetyTest, DefaultSpellPoolHonorsOrdinaryAcquisitionEligibility)
{
	startGame();
	const SpellID masterChainLightning(SpellID::decode("new-horizons:masterChainLightning"));
	const SpellID magicArrow(SpellID::MAGIC_ARROW);
	ASSERT_TRUE(masterChainLightning.hasValue());
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(gameState()->getMagicRules(), masterChainLightning));
	ASSERT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(gameState()->getMagicRules(), masterChainLightning));

	gameState()->getMap().allowedSpells = {masterChainLightning, magicArrow};
	GameRandomizer randomizer(*gameState());
	JsonRandom json(gameState().get(), randomizer);
	const JsonNode defaultSelector(JsonMap{});
	EXPECT_EQ(json.loadSpell(defaultSelector, {}), magicArrow);

	gameState()->getMap().allowedSpells = {masterChainLightning};
	EXPECT_EQ(json.loadSpell(defaultSelector, {}), SpellID(SpellID::NONE));
}

TEST_F(NewHorizonsSpellArraySafetyTest, ExplicitNamedSpecialtySpellKeepsIdentityAndExistingCasting)
{
	startGame();
	const SpellID masterChainLightning(SpellID::decode("new-horizons:masterChainLightning"));
	const SpellID magicArrow(SpellID::MAGIC_ARROW);
	ASSERT_TRUE(masterChainLightning.hasValue());
	gameState()->getMap().allowedSpells = {magicArrow};

	GameRandomizer randomizer(*gameState());
	JsonRandom json(gameState().get(), randomizer);
	JsonNode named("masterChainLightning");
	named.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	EXPECT_EQ(json.loadSpell(named, {}), masterChainLightning)
		<< "Explicit names keep their existing map-ban override and saved-roster gate";

	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const SecondarySkill havoc(SecondarySkill::decode("new-horizons:havocMagic"));
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->canLearnSpell(masterChainLightning.toSpell(), true));
	attackerSideHero->addSpellToSpellbook(masterChainLightning);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(masterChainLightning.toSpell()));
}

TEST_F(NewHorizonsSpellArraySafetyTest, HistoricalDefaultPoolWithoutOrdinaryMarkerRetainsSpecialtySpell)
{
	magicRulesOverride = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	(*magicRulesOverride)["spells"]["new-horizons:masterChainLightning"].Struct().erase("ordinaryAcquisition");
	startGame();
	const SpellID masterChainLightning(SpellID::decode("new-horizons:masterChainLightning"));
	ASSERT_TRUE(masterChainLightning.hasValue());
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(gameState()->getMagicRules(), masterChainLightning));
	ASSERT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(gameState()->getMagicRules(), masterChainLightning));
	gameState()->getMap().allowedSpells = {masterChainLightning};

	GameRandomizer randomizer(*gameState());
	JsonRandom json(gameState().get(), randomizer);
	EXPECT_EQ(json.loadSpell(JsonNode(JsonMap{}), {}), masterChainLightning);
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
