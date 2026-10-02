/*
 * NewHorizonsVerdantPrisonTest.cpp, part of VCMI engine
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
#include <numeric>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
constexpr std::string_view verdantPrisonKey = "new-horizons:verdantPrison";
constexpr std::string_view natureMagicSkill = "new-horizons:natureMagic";
constexpr std::string_view rootcallerPerk = "new-horizons:natureMagic.rootcaller";
constexpr std::string_view verdantWardenPerk = "new-horizons:natureMagic.verdantWarden";
constexpr std::string_view dendroidGuardKey = "core:dendroidGuard";

SpellID verdantPrisonSpell()
{
	return SpellID(SpellID::decode(std::string(verdantPrisonKey)));
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
		3LL * spellPower * modifierPercent, 1, coefficientBasisPoints, 0, 0);
	return (180LL * modifierPercent + spellPowerHundredths) / 100;
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

class NewHorizonsVerdantPrisonTest : public BattleTestFixture
{
protected:
	int magicRulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	CStack * friendly = nullptr;
	CStack * ordinaryGuard = nullptr;
	CStack * enemy = nullptr;
	const BattleHex targetHex{8, 5};

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

	void prepare(const int spellPower = 21, const int rulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION,
		const std::string_view enemyCreature = "core:peasant")
	{
		magicRulesVersion = rulesVersion;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(verdantPrisonSpell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 20);
		ordinaryGuard = addStack(BattleSide::ATTACKER, creatureByName(std::string(dendroidGuardKey)), BattleHex(3, 5), 1);
		enemy = addStack(BattleSide::DEFENDER, creatureByName(std::string(enemyCreature)), targetHex, 20);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(ordinaryGuard, nullptr);
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

	BattleAction prisonAction(const BattleHex & hex) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = verdantPrisonSpell();
		action.aimToHex(hex);
		return action;
	}

	bool castAt(const BattleHex & hex)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), PlayerColor(0), prisonAction(hex));
	}

	bool castAtUnit(const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = verdantPrisonSpell();
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
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
		return battle()->getSpellEffectValue(verdantPrisonSpell().toSpell(), attackerSideHero,
			spells::Mode::HERO, hex);
	}

	std::vector<BattleHex> ringAt(const BattleHex & hex, const spells::Mode mode = spells::Mode::HERO) const
	{
		spells::BattleCast cast(battle(), attackerSideHero, mode,
			verdantPrisonSpell().toSpell());
		auto mechanics = verdantPrisonSpell().toSpell()->battleMechanics(&cast);
		return mechanics->rangeInHexes(hex).toVector();
	}

	int32_t summonedGuardMaxHealth() const
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO,
			verdantPrisonSpell().toSpell());
		auto mechanics = verdantPrisonSpell().toSpell()->battleMechanics(&cast);
		return mechanics->getSummonedCreatureMaxHealth(
			creatureByName(std::string(dendroidGuardKey)).toEntity(LIBRARY), true);
	}

	std::vector<const CStack *> temporaryGuards() const
	{
		return battle()->battleGetStacksIf([](const CStack * unit)
		{
			return unit->isSummoned() && unit->natureSummoned && unit->unitType()
				&& unit->unitType()->getJsonKey() == dendroidGuardKey;
		});
	}

	void selectVerdantWarden()
	{
		const auto decoded = SecondarySkill::decode(std::string(natureMagicSkill));
		ASSERT_GE(decoded, 0);
		const auto nature = SecondarySkill(decoded);
		attackerSideHero->setSecSkillLevel(nature, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({std::string(natureMagicSkill), std::string(rootcallerPerk)});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(natureMagicSkill), std::string(rootcallerPerk)));
		attackerSideHero->setSecSkillLevel(nature, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({std::string(natureMagicSkill), std::string(verdantWardenPerk)});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(natureMagicSkill), std::string(verdantWardenPerk)));
	}

	void activateAttackerStack(CStack * active)
	{
		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -active->getMovementRange();
		active->addNewBonus(std::make_shared<Bonus>(immobilized));

		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = active->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}
};

TEST_F(NewHorizonsVerdantPrisonTest, FullRingGetsExactPoolAndOrdinaryTemporaryCreatureState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = verdantPrisonSpell();
	const auto armyBefore = armySnapshot(attackerSideHero);
	const auto ring = ringAt(enemy->getPosition());
	ASSERT_EQ(ring.size(), 6u);
	EXPECT_TRUE(std::ranges::is_sorted(ring, {}, &BattleHex::toInt));

	const auto forecast = preview(enemy->getPosition());
	ASSERT_NE(forecast, nullptr);
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell);
	EXPECT_EQ(forecast->hpDelta, expectedPool(21, coefficient));
	ASSERT_NE(forecast->unitType, nullptr);
	EXPECT_EQ(forecast->unitType->getJsonKey(), dendroidGuardKey);

	ASSERT_TRUE(castAt(enemy->getPosition()));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000 - 12);
	const auto summoned = temporaryGuards();
	ASSERT_EQ(summoned.size(), ring.size());
	const auto maxHealth = summonedGuardMaxHealth();
	const auto baseShare = forecast->hpDelta / static_cast<int64_t>(ring.size());
	const auto remainder = forecast->hpDelta % static_cast<int64_t>(ring.size());
	int64_t aggregateHealth = 0;
	int64_t creatureCount = 0;
	std::vector<BattleHex> spawnHexes;
	for(const auto * unit : summoned)
	{
		spawnHexes.push_back(unit->getPosition());
		const auto position = std::ranges::find(ring, unit->getPosition());
		ASSERT_NE(position, ring.end());
		const auto index = static_cast<int64_t>(std::distance(ring.begin(), position));
		const auto share = baseShare + (index < remainder ? 1 : 0);
		EXPECT_EQ(unit->getAvailableHealth(), share);
		EXPECT_EQ(unit->getCount(), (share + maxHealth - 1) / maxHealth);
		EXPECT_EQ(unit->unitSide(), BattleSide::ATTACKER);
		EXPECT_TRUE(unit->isSummoned());
		EXPECT_TRUE(unit->natureSummoned);
		EXPECT_EQ(unit->getInitiative(), ordinaryGuard->getInitiative());
		EXPECT_EQ(unit->getAllBonuses(Selector::type()(BonusType::SPELL_AFTER_ATTACK))->size(),
			ordinaryGuard->getAllBonuses(Selector::type()(BonusType::SPELL_AFTER_ATTACK))->size());
		aggregateHealth += unit->getAvailableHealth();
		creatureCount += unit->getCount();
	}
	std::ranges::sort(spawnHexes, {}, &BattleHex::toInt);
	EXPECT_EQ(spawnHexes, ring);
	EXPECT_EQ(aggregateHealth, forecast->hpDelta);
	EXPECT_EQ(creatureCount, forecast->unitsDelta);
	EXPECT_EQ(armySnapshot(attackerSideHero), armyBefore)
		<< "Temporary battlefield guards must not enter the hero's strategic army";
	EXPECT_TRUE(enemy->getAllBonuses(Selector::type()(BonusType::BIND_EFFECT))->empty())
		<< "The spell must not bind the selected target or make summons depend on it";

	// UnitInfo and mutable UnitState JSON are the authoritative spawn payloads.
	const auto * first = summoned.front();
	battle::UnitInfo spawnInfo;
	spawnInfo.id = first->unitId();
	spawnInfo.count = first->getCount();
	spawnInfo.type = first->creatureId();
	spawnInfo.side = first->unitSide();
	spawnInfo.position = first->getPosition();
	spawnInfo.summoned = first->acquireState()->summoned;
	spawnInfo.natureSummoned = first->acquireState()->natureSummoned;
	JsonNode spawnData;
	spawnInfo.save(spawnData);
	battle::UnitInfo restoredSpawnInfo;
	restoredSpawnInfo.load(spawnInfo.id, spawnData);
	EXPECT_TRUE(restoredSpawnInfo.summoned);
	EXPECT_TRUE(restoredSpawnInfo.natureSummoned);
	EXPECT_EQ(restoredSpawnInfo.position, first->getPosition());

	auto stateRoundTrip = first->acquireState();
	const auto stateData = stateRoundTrip->save();
	stateRoundTrip->load(stateData);
	EXPECT_TRUE(stateRoundTrip->summoned);
	EXPECT_TRUE(stateRoundTrip->natureSummoned);
	EXPECT_EQ(stateRoundTrip->getAvailableHealth(), first->getAvailableHealth());
	UnitChanges stateUpdate(first->unitId(), UnitChanges::EOperation::UPDATE);
	stateUpdate.data = stateRoundTrip->save();
	BattleUnitsChanged updatePack;
	updatePack.battleID = BattleID(0);
	updatePack.changedStacks.push_back(std::move(stateUpdate));
	gameHandler->sendAndApply(updatePack);
	const auto * updated = battle()->battleGetUnitByID(first->unitId());
	ASSERT_NE(updated, nullptr);
	EXPECT_TRUE(updated->acquireState()->summoned);
	EXPECT_TRUE(updated->acquireState()->natureSummoned);
	EXPECT_EQ(updated->getAvailableHealth(), first->getAvailableHealth());

	const auto casts = server.castsOf(spell);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [&](const std::string & line)
	{
		return line.find(std::to_string(forecast->hpDelta)) != std::string::npos
			&& line.find(std::to_string(creatureCount)) != std::string::npos
			&& line.find(std::to_string(summoned.size()) + " stacks") != std::string::npos
			&& line.find("aggregate HP") != std::string::npos;
	}));

	const auto guardCountBeforeTargetMoves = temporaryGuards().size();
	BattleStackMoved escape;
	escape.battleID = BattleID(0);
	escape.stack = enemy->unitId();
	escape.teleporting = true;
	escape.tilesToMove.insert(BattleHex(14, 5));
	gameHandler->sendAndApply(escape);
	EXPECT_EQ(temporaryGuards().size(), guardCountBeforeTargetMoves)
		<< "Moving the original target away must not remove the temporary guards";
}

TEST_F(NewHorizonsVerdantPrisonTest, PartialRingPreviewAndApplyUseTheSameSortedRemainderAllocation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto fullRing = ringAt(enemy->getPosition());
	ASSERT_EQ(fullRing.size(), 6u);
	for(size_t index = 0; index < 2; ++index)
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), fullRing[index], 1), nullptr);

	const auto ring = ringAt(enemy->getPosition());
	ASSERT_EQ(ring.size(), 4u);
	EXPECT_TRUE(std::ranges::is_sorted(ring, {}, &BattleHex::toInt));
	const auto forecast = preview(enemy->getPosition());
	ASSERT_NE(forecast, nullptr);

	ASSERT_TRUE(castAt(enemy->getPosition()));
	const auto summoned = temporaryGuards();
	ASSERT_EQ(summoned.size(), ring.size());
	const auto baseShare = forecast->hpDelta / static_cast<int64_t>(ring.size());
	const auto remainder = forecast->hpDelta % static_cast<int64_t>(ring.size());
	const auto maxHealth = summonedGuardMaxHealth();
	int64_t healthSum = 0;
	int64_t countSum = 0;
	for(const auto * unit : summoned)
	{
		const auto position = std::ranges::find(ring, unit->getPosition());
		ASSERT_NE(position, ring.end());
		const auto index = static_cast<int64_t>(std::distance(ring.begin(), position));
		const auto share = baseShare + (index < remainder ? 1 : 0);
		EXPECT_EQ(unit->getAvailableHealth(), share);
		EXPECT_EQ(unit->getCount(), (share + maxHealth - 1) / maxHealth);
		healthSum += unit->getAvailableHealth();
		countSum += unit->getCount();
	}
	EXPECT_EQ(healthSum, forecast->hpDelta);
	EXPECT_EQ(countSum, forecast->unitsDelta);
}

TEST_F(NewHorizonsVerdantPrisonTest, DoubleWideTargetUsesTheUnionFootprintFromEitherOccupiedHex)
{
	// Centaur is double-wide but has no level-3 spell immunity, so this exercises
	// the footprint union rather than testing the dragon's unrelated immunity.
	ASSERT_NO_FATAL_FAILURE(prepare(21, newHorizonsMagic::CURRENT_RULESET_VERSION, "core:centaur"));
	ASSERT_TRUE(enemy->doubleWide());
	const auto headRing = ringAt(enemy->getPosition());
	const auto tailRing = ringAt(enemy->occupiedHex());
	EXPECT_EQ(headRing, tailRing);
	ASSERT_GT(headRing.size(), 6u);

	const auto fromHead = preview(enemy->getPosition());
	const auto fromTail = preview(enemy->occupiedHex());
	ASSERT_NE(fromHead, nullptr);
	ASSERT_NE(fromTail, nullptr);
	EXPECT_EQ(fromHead->hpDelta, fromTail->hpDelta);
	EXPECT_EQ(fromHead->unitsDelta, fromTail->unitsDelta);
	activateAttackerStack(friendly);
	ASSERT_TRUE(castAt(enemy->occupiedHex()));
	const auto summoned = temporaryGuards();
	ASSERT_EQ(summoned.size(), headRing.size());
	std::vector<BattleHex> positions;
	for(const auto * unit : summoned)
		positions.push_back(unit->getPosition());
	std::ranges::sort(positions, {}, &BattleHex::toInt);
	EXPECT_EQ(positions, headRing);
}

TEST_F(NewHorizonsVerdantPrisonTest, WardenAppliesToWholeFractionalSchoolScaledPoolBeforeFinalFloor)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	selectVerdantWarden();
	const auto spell = verdantPrisonSpell();
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell);
	ASSERT_EQ(coefficient, 13000);
	const auto forecast = preview(enemy->getPosition());
	ASSERT_NE(forecast, nullptr);
	EXPECT_EQ(forecast->hpDelta, 229)
		<< "floor((180 + 3 x 1 x 130%) x 125%) is 229";
	EXPECT_EQ(forecast->hpDelta, expectedPool(1, coefficient, 125));
	const auto legalRingBeforeCast = ringAt(enemy->getPosition());
	ASSERT_FALSE(legalRingBeforeCast.empty());

	ASSERT_TRUE(castAt(enemy->getPosition()));
	const auto summoned = temporaryGuards();
	ASSERT_EQ(summoned.size(), legalRingBeforeCast.size());
	EXPECT_EQ(std::accumulate(summoned.begin(), summoned.end(), int64_t{0},
		[](const int64_t total, const CStack * unit) { return total + unit->getAvailableHealth(); }),
		forecast->hpDelta);
}

TEST_F(NewHorizonsVerdantPrisonTest, NoLegalRingRejectsBeforeManaOrHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto fullRing = ringAt(enemy->getPosition());
	for(const auto & hex : fullRing)
		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), hex, 1), nullptr);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castAt(enemy->getPosition()));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(temporaryGuards().empty());
	ASSERT_TRUE(castHaste()) << "An empty-ring rejection must preserve the Hero Action";
}
TEST_F(NewHorizonsVerdantPrisonTest, SavedV2RejectsBeforeManaOrHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(21, newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION));
	const auto v2ManaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castAt(enemy->getPosition()));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), v2ManaBefore);
	EXPECT_TRUE(temporaryGuards().empty());
	ASSERT_TRUE(castHaste()) << "A saved-v2 rejection must preserve the Hero Action";
}

TEST_F(NewHorizonsVerdantPrisonTest, FullMagicResistanceRejectsBeforeResourcesOrSummoning)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castAtUnit(enemy))
		<< "Normal target receptivity rejects 100% spell resistance before a cast is accepted";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(temporaryGuards().empty())
		<< "A fully resistant target must not be transformed into a ring-only summon target";
}

TEST_F(NewHorizonsVerdantPrisonTest, MagicMirrorRedirectsTheAnchorToTheCasterSide)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto enemyRing = ringAt(enemy->getPosition());
	std::vector<BattleHex> reflectedRing = ringAt(friendly->getPosition(), spells::Mode::MAGIC_MIRROR);
	const auto secondReflectedRing = ringAt(ordinaryGuard->getPosition(), spells::Mode::MAGIC_MIRROR);
	reflectedRing.insert(reflectedRing.end(), secondReflectedRing.begin(), secondReflectedRing.end());
	std::ranges::sort(reflectedRing, {}, &BattleHex::toInt);
	reflectedRing.erase(std::unique(reflectedRing.begin(), reflectedRing.end()), reflectedRing.end());
	ASSERT_FALSE(reflectedRing.empty());
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_MIRROR, BonusSource::OTHER, 100, BonusSourceID()));
	ASSERT_TRUE(castAtUnit(enemy));
	const auto summoned = temporaryGuards();
	ASSERT_FALSE(summoned.empty());
	EXPECT_TRUE(std::ranges::all_of(summoned, [](const CStack * unit)
	{
		return unit->unitSide() == BattleSide::ATTACKER && unit->isSummoned() && unit->natureSummoned;
	}));
	for(const auto * unit : summoned)
	{
		EXPECT_NE(std::ranges::find(reflectedRing, unit->getPosition()), reflectedRing.end())
			<< "Magic Mirror must place the ring around the redirected friendly anchor";
		EXPECT_EQ(std::ranges::find(enemyRing, unit->getPosition()), enemyRing.end())
			<< "A reflected cast must not resolve around the original enemy target";
	}
}
