/*
 * NewHorizonsSummonTrollsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/effects/Effect.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
constexpr auto summonTrollsKey = "new-horizons:summonTrolls";
constexpr auto natureMagicSkill = "new-horizons:natureMagic";
constexpr auto beastcallerPerk = "new-horizons:natureMagic.beastcaller";

SpellID summonTrollsSpell()
{
	return SpellID(SpellID::decode(summonTrollsKey));
}

JsonNode magicRulesAtVersion(const int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
	if(version < newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		rules.Struct().erase("schoolRankPowerCoefficientPercent");
		rules.Struct().erase("spellcraftEfficiencyPercent");
		for(auto & [name, spell] : rules["spells"].Struct())
		{
			(void)name;
			spell.Struct().erase("selectedPlacement");
			if(spell.Struct().contains("variant"))
			{
				spell.Struct().erase("variant");
				spell["active"].Bool() = false;
			}
		}
	}
	if(version == newHorizonsMagic::RULESET_VERSION)
	{
		rules.Struct().erase("spellPoints");
		rules.Struct().erase("mageGuildGeneration");
		rules.Struct().erase("physicalDamageReductionCapPercent");
		rules.Struct().erase("warcasting");
		for(auto & [name, spell] : rules["spells"].Struct())
		{
			(void)name;
			spell.Struct().erase("active");
			spell.Struct().erase("directDamage");
			spell.Struct().erase("cureAfflictions");
		}
	}
	return rules;
}

int64_t expectedPool(const int spellPower, const int coefficientBasisPoints,
	const int modifierPercent = 100)
{
	const int64_t spellPowerHundredths = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		5LL * spellPower * modifierPercent, 2, coefficientBasisPoints, 0, 0);
	return (100LL * modifierPercent + spellPowerHundredths) / 100;
}

using ArmySnapshot = std::vector<std::tuple<int, CreatureID, TQuantity>>;

ArmySnapshot armySnapshot(const CGHeroInstance * hero)
{
	ArmySnapshot result;
	for(const auto & [slot, stack] : hero->Slots())
		result.emplace_back(slot.getNum(), stack->getCreatureID(), stack->getCount());
	return result;
}
}

class NewHorizonsSummonTrollsTest : public BattleTestFixture
{
protected:
	int magicRulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;
	CStack * ordinaryTroll = nullptr;
	const BattleHex firstSummonHex{8, 5};
	const BattleHex secondSummonHex{9, 5};

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			magicRulesAtVersion(magicRulesVersion));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(const int spellPower = 21,
		const int rulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION,
		const std::string_view healthArtifactKey = {})
	{
		magicRulesVersion = rulesVersion;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(summonTrollsSpell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		if(!healthArtifactKey.empty())
			giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode(std::string(healthArtifactKey))),
				ArtifactPosition::MISC1);

		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 20);
		ordinaryTroll = addStack(BattleSide::ATTACKER, creatureByName("core:troll"), BattleHex(3, 5), 1);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(14, 5), 20);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(ordinaryTroll, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	BattleAction summonAction(const BattleHex & hex) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = summonTrollsSpell();
		action.aimToHex(hex);
		return action;
	}

	bool castAt(const BattleHex & hex)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), PlayerColor(0), summonAction(hex));
	}

	bool castHaste()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(friendly);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	SpellEffectValUptr preview(const BattleHex & hex) const
	{
		return battle()->getSpellEffectValue(summonTrollsSpell().toSpell(), attackerSideHero,
			spells::Mode::HERO, hex);
	}

	std::vector<const CStack *> temporaryTrolls() const
	{
		return battle()->battleGetStacksIf([](const CStack * unit)
		{
			return unit->isSummoned() && unit->natureSummoned
				&& unit->unitSide() == BattleSide::ATTACKER
				&& unit->unitType() && unit->unitType()->getJsonKey() == "core:troll";
		});
	}

	void selectBeastcaller()
	{
		const auto decoded = SecondarySkill::decode(natureMagicSkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({natureMagicSkill, beastcallerPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(natureMagicSkill, beastcallerPerk));
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
};

TEST_F(NewHorizonsSummonTrollsTest, CastMatchesPreviewAndKeepsTheChosenTemporaryTrollStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = summonTrollsSpell();
	const auto armyBefore = armySnapshot(attackerSideHero);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto forecast = preview(firstSummonHex);
	ASSERT_NE(forecast, nullptr);
	EXPECT_EQ(forecast->hpDelta, 152);
	EXPECT_EQ(forecast->unitsDelta, 4);

	ASSERT_TRUE(castAt(firstSummonHex));
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 9);
	const auto summoned = temporaryTrolls();
	ASSERT_EQ(summoned.size(), 1u);
	EXPECT_EQ(summoned.front()->getPosition(), firstSummonHex);
	EXPECT_EQ(summoned.front()->getCount(), forecast->unitsDelta);
	EXPECT_EQ(summoned.front()->getAvailableHealth(), forecast->hpDelta);
	EXPECT_TRUE(summoned.front()->isSummoned());
	EXPECT_TRUE(summoned.front()->natureSummoned);
	EXPECT_EQ(summoned.front()->getInitiative(), ordinaryTroll->getInitiative());
	EXPECT_GT(summoned.front()->getAllBonuses(Selector::type()(BonusType::HP_REGENERATION))->size(), 0u);
	EXPECT_EQ(armySnapshot(attackerSideHero), armyBefore)
		<< "The temporary battle stack must not enter the hero's strategic army";

	// UnitInfo JSON is the actual ADD payload and carries spawn provenance;
	// CStack's binary BattleInfo serialization intentionally excludes unit state.
	battle::UnitInfo spawnInfo;
	spawnInfo.id = summoned.front()->unitId();
	spawnInfo.count = summoned.front()->getCount();
	spawnInfo.type = summoned.front()->creatureId();
	spawnInfo.side = summoned.front()->unitSide();
	spawnInfo.position = summoned.front()->getPosition();
	spawnInfo.summoned = summoned.front()->acquireState()->summoned;
	spawnInfo.natureSummoned = summoned.front()->acquireState()->natureSummoned;
	JsonNode spawnData;
	spawnInfo.save(spawnData);
	battle::UnitInfo restoredSpawnInfo;
	restoredSpawnInfo.load(spawnInfo.id, spawnData);
	EXPECT_TRUE(restoredSpawnInfo.summoned);
	EXPECT_TRUE(restoredSpawnInfo.natureSummoned);
	EXPECT_EQ(restoredSpawnInfo.count, forecast->unitsDelta);
	EXPECT_EQ(restoredSpawnInfo.position, firstSummonHex);

	// Mutable unit state travels in UnitChanges JSON. Round-trip the acquired
	// state, then replay it through the authoritative battle UPDATE path.
	auto stateRoundTrip = summoned.front()->acquireState();
	const auto stateData = stateRoundTrip->save();
	stateRoundTrip->load(stateData);
	EXPECT_TRUE(stateRoundTrip->summoned);
	EXPECT_TRUE(stateRoundTrip->natureSummoned);
	EXPECT_EQ(stateRoundTrip->getCount(), forecast->unitsDelta);
	EXPECT_EQ(stateRoundTrip->getAvailableHealth(), forecast->hpDelta);

	UnitChanges stateUpdate(summoned.front()->unitId(), UnitChanges::EOperation::UPDATE);
	stateUpdate.data = stateRoundTrip->save();
	BattleUnitsChanged updatePack;
	updatePack.battleID = BattleID(0);
	updatePack.changedStacks.push_back(std::move(stateUpdate));
	gameHandler->sendAndApply(updatePack);
	const auto * updatedTroll = battle()->battleGetUnitByID(summoned.front()->unitId());
	ASSERT_NE(updatedTroll, nullptr);
	const auto updatedState = updatedTroll->acquireState();
	EXPECT_TRUE(updatedState->summoned);
	EXPECT_TRUE(updatedState->natureSummoned);
	EXPECT_EQ(updatedTroll->getCount(), forecast->unitsDelta);
	EXPECT_EQ(updatedTroll->getAvailableHealth(), forecast->hpDelta);
	EXPECT_EQ(updatedTroll->getPosition(), firstSummonHex);

	const auto casts = server.castsOf(spell);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("152") != std::string::npos
			&& line.find("aggregate HP") != std::string::npos
			&& line.find("temporary Nature summon") != std::string::npos;
	}));
}

TEST_F(NewHorizonsSummonTrollsTest, NatureRankScalesOnlyTheSpellPowerComponent)
{
	constexpr int spellPower = 41;
	ASSERT_NO_FATAL_FAILURE(prepare(spellPower));
	const auto spell = summonTrollsSpell();
	const auto nature = SecondarySkill(SecondarySkill::decode(natureMagicSkill));
	ASSERT_GE(nature.getNum(), 0);
	const std::array<int, 4> ranks{
		MasteryLevel::NONE, MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT};
	SpellEffectValUptr expertForecast;

	for(const auto rank : ranks)
	{
		attackerSideHero->setSecSkillLevel(nature, rank, ChangeValueMode::ABSOLUTE);
		const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			battle()->getMagicRules(), attackerSideHero, spell);
		auto forecast = preview(firstSummonHex);
		ASSERT_NE(forecast, nullptr);
		const auto pool = expectedPool(spellPower, coefficient);
		EXPECT_EQ(forecast->hpDelta, pool) << "Nature rank " << rank;
		EXPECT_EQ(forecast->unitsDelta, (pool + ordinaryTroll->getMaxHealth() - 1)
			/ ordinaryTroll->getMaxHealth()) << "Nature rank " << rank;
		if(rank == MasteryLevel::EXPERT)
			expertForecast = std::move(forecast);
	}

	ASSERT_TRUE(castAt(firstSummonHex));
	const auto summoned = temporaryTrolls();
	ASSERT_EQ(summoned.size(), 1u);
	ASSERT_NE(expertForecast, nullptr);
	EXPECT_EQ(summoned.front()->getAvailableHealth(), expertForecast->hpDelta);
}

TEST_F(NewHorizonsSummonTrollsTest, BeastcallerScalesTheUnroundedWholePoolBeforeFinalFloor)
{
	constexpr int spellPower = 1;
	ASSERT_NO_FATAL_FAILURE(prepare(spellPower));
	selectBeastcaller();
	const auto spell = summonTrollsSpell();
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell);
	const auto forecast = preview(firstSummonHex);
	ASSERT_NE(forecast, nullptr);
	EXPECT_EQ(forecast->hpDelta, 128)
		<< "floor((100 + 2.5 x 1 x 115%) x 125%) is 128, not 127";
	EXPECT_EQ(forecast->hpDelta, expectedPool(spellPower, coefficient, 125));

	ASSERT_TRUE(castAt(firstSummonHex));
	const auto summoned = temporaryTrolls();
	ASSERT_EQ(summoned.size(), 1u);
	EXPECT_EQ(summoned.front()->getAvailableHealth(), forecast->hpDelta);
	EXPECT_EQ(summoned.front()->getCount(), 4);
}

TEST_F(NewHorizonsSummonTrollsTest, AdditiveHeroHealthBonusMatchesPreviewCountAndExactPool)
{
	constexpr int spellPower = 57;
	ASSERT_NO_FATAL_FAILURE(prepare(spellPower, newHorizonsMagic::CURRENT_RULESET_VERSION,
		"core:ringOfVitality"));
	ASSERT_EQ(ordinaryTroll->getMaxHealth(), 41);
	const auto forecast = preview(firstSummonHex);
	ASSERT_NE(forecast, nullptr);
	EXPECT_EQ(forecast->hpDelta, 242);
	EXPECT_EQ(forecast->unitsDelta, 6);

	ASSERT_TRUE(castAt(firstSummonHex));
	const auto summoned = temporaryTrolls();
	ASSERT_EQ(summoned.size(), 1u);
	EXPECT_EQ(summoned.front()->getCount(), forecast->unitsDelta);
	EXPECT_EQ(summoned.front()->getAvailableHealth(), forecast->hpDelta);
}

TEST_F(NewHorizonsSummonTrollsTest, PercentToBaseHeroHealthBonusMatchesPreviewCount)
{
	constexpr int spellPower = 57;
	ASSERT_NO_FATAL_FAILURE(prepare(spellPower, newHorizonsMagic::CURRENT_RULESET_VERSION,
		"core:elixirOfLife"));
	ASSERT_EQ(ordinaryTroll->getMaxHealth(), 54);
	const auto forecast = preview(firstSummonHex);
	ASSERT_NE(forecast, nullptr);
	EXPECT_EQ(forecast->hpDelta, 242);
	EXPECT_EQ(forecast->unitsDelta, 5);

	ASSERT_TRUE(castAt(firstSummonHex));
	const auto summoned = temporaryTrolls();
	ASSERT_EQ(summoned.size(), 1u);
	EXPECT_EQ(summoned.front()->getCount(), forecast->unitsDelta);
	EXPECT_EQ(summoned.front()->getAvailableHealth(), forecast->hpDelta);
}

TEST_F(NewHorizonsSummonTrollsTest, OccupiedSelectedHexRejectsBeforeManaOrHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castAt(friendly->occupiedHex()));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(temporaryTrolls().empty());

	ASSERT_TRUE(castHaste()) << "The rejected placement must leave the Hero Action available";
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
}

TEST_F(NewHorizonsSummonTrollsTest, LaterRoundCreatesAnIndependentStackWithoutCombatWideRecastLock)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castAt(firstSummonHex));
	const auto first = temporaryTrolls();
	ASSERT_EQ(first.size(), 1u);
	EXPECT_FALSE(castAt(secondSummonHex)) << "The current-round Hero Action is already spent";

	advanceRound();
	ASSERT_TRUE(castAt(secondSummonHex));
	const auto summoned = temporaryTrolls();
	ASSERT_EQ(summoned.size(), 2u);
	EXPECT_NE(summoned[0]->getPosition(), summoned[1]->getPosition());
	EXPECT_EQ(summoned[0]->getAvailableHealth(), summoned[1]->getAvailableHealth());
}

TEST_F(NewHorizonsSummonTrollsTest, SavedV2RejectsBeforeManaAndHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(21, newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castAt(firstSummonHex));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(temporaryTrolls().empty());
	ASSERT_TRUE(castHaste()) << "A saved-v2 rejection must not consume the Hero Action";
}
