/*
 * NewHorizonsElementalRebirthTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/CStack.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view REBIRTH_SKILL = "new-horizons:elementalRebirth";

HeroTypeID heroType(const char * identifier)
{
	const int decoded = HeroTypeID::decode(identifier);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing hero in Elemental Rebirth fixture: ") + identifier);
	return HeroTypeID(decoded);
}

CreatureID creature(const char * identifier)
{
	const int decoded = CreatureID::decode(identifier);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing creature in Elemental Rebirth fixture: ") + identifier);
	return CreatureID(decoded);
}
}

class NewHorizonsElementalRebirthTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons native profile";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(int rank, int sourceCount = 7, const char * sourceCreature = "core:peasant",
		bool includeCloneStack = false, bool selectPrimalBurst = false,
		bool selectGreaterEssence = false)
	{
		startGame();
		defenderSideHero->setHeroType(heroType("core:brissa"));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();

		const auto skillNumber = SecondarySkill::decode(std::string(REBIRTH_SKILL));
		ASSERT_GE(skillNumber, 0);
		defenderSideHero->setSecSkillLevel(SecondarySkill(skillNumber), rank, ChangeValueMode::ABSOLUTE);
		if(selectGreaterEssence)
			selectPrimalBurst = true;
		if(selectPrimalBurst)
			defenderSideHero->applyPerkSelection({std::string(REBIRTH_SKILL),
				"new-horizons:elementalRebirth.primalBurst"});
		if(selectGreaterEssence)
			defenderSideHero->applyPerkSelection({std::string(REBIRTH_SKILL),
				"new-horizons:elementalRebirth.greaterEssence"});
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature(sourceCreature), sourceCount));
		if(includeCloneStack)
			ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creature(sourceCreature), sourceCount));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), 1));
		ASSERT_EQ(newHorizonsElementalAttunement::captureProfile(defenderSideHero).rank, rank);

		startBattle();
		source = stackAt(BattleSide::DEFENDER, SlotID(0));
		attacker = stackAt(BattleSide::ATTACKER, SlotID(0));
		ASSERT_NE(source, nullptr);
		ASSERT_NE(attacker, nullptr);
		basisAtStart = source->getAvailableHealth();
		ASSERT_GT(basisAtStart, 0);
		ASSERT_EQ(basisAtStart, static_cast<int64_t>(sourceCount) * source->getMaxHealth());
		beginCombat();
	}

	uint32_t addTemporaryStack(BattleSide side, const CreatureID & type,
		const BattleHex & position, int32_t count)
	{
		battle::UnitInfo info;
		info.id = battle()->battleNextUnitId();
		info.count = count;
		info.type = type;
		info.side = side;
		info.position = position;
		info.summoned = true;
		BattleUnitsChanged add;
		add.battleID = BattleID(0);
		add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
		info.save(add.changedStacks.back().data);
		gameHandler->sendAndApply(add);
		return info.id;
	}

	CStack * stackAt(BattleSide side, SlotID slot) const
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit && unit->unitSide() == side && unit->unitSlot() == slot)
				return battle()->getStack(unit->unitId(), false);
		return nullptr;
	}

	void applyInjury(CStack * target, int64_t damage, bool asBattleAttack = false,
		std::optional<bool> expectDeath = true, std::optional<bool> expectNativeRebirth = std::nullopt)
	{
		ASSERT_NE(target, nullptr);
		ASSERT_NE(attacker, nullptr);
		BattleStackAttacked hit;
		hit.attackerID = attacker->unitId();
		hit.stackAttacked = target->unitId();
		hit.damageAmount = damage;
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), target->acquireState());
		if(expectDeath)
		{
			EXPECT_EQ(hit.killed(), *expectDeath);
		}
		if(expectNativeRebirth)
		{
			EXPECT_EQ(hit.willRebirth(), *expectNativeRebirth);
		}

		if(asBattleAttack)
		{
			BattleAttack pack;
			pack.battleID = BattleID(0);
			pack.stackAttacking = attacker->unitId();
			pack.attackerChanges.battleID = BattleID(0);
			UnitChanges attackerUpdate(attacker->unitId(), UnitChanges::EOperation::UPDATE);
			attackerUpdate.data = attacker->acquireState()->save();
			pack.attackerChanges.changedStacks.push_back(std::move(attackerUpdate));
			pack.bsa.push_back(std::move(hit));
			gameHandler->sendAndApply(pack);
		}
		else
		{
			StacksInjured pack;
			pack.battleID = BattleID(0);
			pack.stacks.push_back(std::move(hit));
			gameHandler->sendAndApply(pack);
		}
	}

	std::vector<const battle::Unit *> summonedUnits() const
	{
		std::vector<const battle::Unit *> result;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit && unit->alive() && unit->isSummoned())
				result.push_back(unit);
		return result;
	}

	CStack * source = nullptr;
	CStack * attacker = nullptr;
	int64_t basisAtStart = 0;
};

TEST_F(NewHorizonsElementalRebirthTest, BasicAttunementDeathCreatesNoReplacementOrExtraAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC));
	const auto roster = battle()->battleGetAllUnits(false).size();
	const auto additions = server.unitAdditionRounds.size();
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
	const auto serial = battle()->getActivationSerial();
	applyInjury(source, 1, false, false);
	EXPECT_EQ(source->getAvailableHealth(), basisAtStart - 1);
	applyInjury(source, source->getAvailableHealth());
	EXPECT_FALSE(source->alive());
	EXPECT_TRUE(summonedUnits().empty());
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), roster);
	EXPECT_EQ(server.unitAdditionRounds.size(), additions);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), allowances);
	EXPECT_EQ(battle()->getActivationSerial(), serial);
}

TEST_F(NewHorizonsElementalRebirthTest, AdvancedRankDeathNeverCreatesAnEliteElemental)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED));
	const auto additions = server.unitAdditionRounds.size();
	applyInjury(source, source->getAvailableHealth());
	EXPECT_FALSE(source->alive());
	EXPECT_TRUE(summonedUnits().empty());
	EXPECT_EQ(server.unitAdditionRounds.size(), additions);
}

TEST_F(NewHorizonsElementalRebirthTest, ExpertBattleAttackDeathDoesNotDispatchReplacementOrInitiativeInsertion)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::EXPERT));
	const auto roster = battle()->battleGetAllUnits(false).size();
	const auto serial = battle()->getActivationSerial();
	const auto activations = server.stackActivations.size();
	applyInjury(source, source->getAvailableHealth(), true);
	EXPECT_FALSE(source->alive());
	EXPECT_TRUE(summonedUnits().empty());
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), roster);
	EXPECT_EQ(server.stackActivations.size(), activations);
	EXPECT_EQ(battle()->getActivationSerial(), serial);
}

TEST_F(NewHorizonsElementalRebirthTest, WaterVeilStablePerkDoesNotExplodeOnDeath)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 1000, "core:peasant", false, true));
	std::vector<BattleHex> empty;
	for(const auto hex : source->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex, false)) empty.push_back(hex);
	ASSERT_GE(empty.size(), 2u);
	const auto first = addTemporaryStack(BattleSide::ATTACKER, creature("core:pikeman"), empty[0], 20);
	const auto second = addTemporaryStack(BattleSide::ATTACKER, creature("core:pikeman"), empty[1], 20);
	const auto firstHealth = battle()->battleGetUnitByID(first)->getAvailableHealth();
	const auto secondHealth = battle()->battleGetUnitByID(second)->getAvailableHealth();
	const auto additions = server.unitAdditionRounds.size();
	applyInjury(source, source->getAvailableHealth());
	EXPECT_EQ(battle()->battleGetUnitByID(first)->getAvailableHealth(), firstHealth);
	EXPECT_EQ(battle()->battleGetUnitByID(second)->getAvailableHealth(), secondHealth);
	EXPECT_EQ(server.unitAdditionRounds.size(), additions);
}

TEST_F(NewHorizonsElementalRebirthTest, GreaterEssencePreservesLegalBasicSelectionWithoutReplacement)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED, 100, "core:earthElemental", false, false, true));
	EXPECT_TRUE(defenderSideHero->hasActivePerk(std::string(REBIRTH_SKILL), "new-horizons:elementalRebirth.primalBurst"));
	const auto profile = newHorizonsElementalAttunement::captureProfile(defenderSideHero);
	EXPECT_TRUE(profile.waterVeil);
	EXPECT_TRUE(profile.greaterEssence);
	EXPECT_EQ(source->elementalAttunementPassives().maximumHealthPercent, 25);
	const auto additions = server.unitAdditionRounds.size();
	applyInjury(source, source->getAvailableHealth());
	EXPECT_EQ(server.unitAdditionRounds.size(), additions);
	EXPECT_EQ(newHorizonsElementalAttunement::captureProfile(defenderSideHero), profile);
}

TEST_F(NewHorizonsElementalRebirthTest, SummonedAndCloneDeathsDoNotRebirth)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 7, "core:peasant", true));
	CStack * clone = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit && unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(1))
			clone = battle()->getStack(unit->unitId(), false);
	ASSERT_NE(clone, nullptr);

	BattleHex summonPosition = BattleHex::INVALID;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && !battle()->battleGetUnitByPos(candidate, false))
		{
			summonPosition = candidate;
			break;
		}
	}
	ASSERT_TRUE(summonPosition.isValid());
	// Battle-added units receive the summoned-slot identity that the runtime
	// `isSummoned()` predicate actually checks.
	battle::UnitInfo summonedInfo;
	summonedInfo.id = battle()->battleNextUnitId();
	summonedInfo.count = 1;
	summonedInfo.type = creature("core:peasant");
	summonedInfo.side = BattleSide::DEFENDER;
	summonedInfo.position = summonPosition;
	summonedInfo.summoned = true;
	BattleUnitsChanged addSummoned;
	addSummoned.battleID = BattleID(0);
	addSummoned.changedStacks.emplace_back(summonedInfo.id, UnitChanges::EOperation::ADD);
	summonedInfo.save(addSummoned.changedStacks.back().data);
	gameHandler->sendAndApply(addSummoned);
	auto * summoned = battle()->getStack(summonedInfo.id, false);
	ASSERT_NE(summoned, nullptr);
	ASSERT_TRUE(summoned->isSummoned());
	applyInjury(summoned, summoned->getAvailableHealth());
	EXPECT_TRUE(summonedUnits().empty());

	// Clone state is carried by an accepted unit-state update, as in other
	// native battle tests; direct mutation of the live CStack is not a packet.
	auto cloneState = clone->acquireState();
	cloneState->cloned = true;
	UnitChanges markClone(clone->unitId(), UnitChanges::EOperation::UPDATE);
	markClone.data = cloneState->save();
	BattleUnitsChanged updateClone;
	updateClone.battleID = BattleID(0);
	updateClone.changedStacks.push_back(std::move(markClone));
	gameHandler->sendAndApply(updateClone);
	ASSERT_TRUE(clone->isClone());
	applyInjury(clone, clone->getAvailableHealth());
	EXPECT_TRUE(clone->isClone());
	ASSERT_EQ(server.injuries.back().stacks.size(), 1u);
	EXPECT_TRUE(server.injuries.back().stacks.front().cloneKilled());
	EXPECT_EQ(server.injuries.back().stacks.front().flags & BattleStackAttacked::KILLED, 0u);
	EXPECT_TRUE(summonedUnits().empty());
}

TEST_F(NewHorizonsElementalRebirthTest, NativePhoenixRebirthSurvivorDoesNotCreateElemental)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 5, "core:phoenix"));
	const auto originalCount = battle()->battleGetAllUnits(false).size();
	applyInjury(source, source->getAvailableHealth(), false, std::nullopt, true);

	ASSERT_TRUE(source->alive()) << "The Phoenix's own Rebirth should leave its stack alive";
	EXPECT_TRUE(summonedUnits().empty());
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), originalCount);
	EXPECT_TRUE(std::ranges::none_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Elemental Rebirth summons") != std::string::npos;
	}));
}

TEST_F(NewHorizonsElementalRebirthTest, UnitInfoPreservesRebirthOriginalHPAsExactInt64)
{
	battle::UnitInfo info;
	info.id = 99;
	info.count = 1;
	info.type = creature("core:fireElemental");
	info.side = BattleSide::DEFENDER;
	info.position = BattleHex(0);
	info.summoned = true;
	info.rebirthOriginalAggregateHP = 9007199254740993LL; // 2^53 + 1

	JsonNode data;
	info.save(data);
	EXPECT_EQ(data["rebirthOriginalAggregateHP"].Integer(), 9007199254740993LL);
	battle::UnitInfo restored;
	restored.load(info.id, data);
	EXPECT_EQ(restored.rebirthOriginalAggregateHP, info.rebirthOriginalAggregateHP);

	BattleUnitsChanged add;
	add.battleID = BattleID(0);
	add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	add.changedStacks.back().data = data;
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE;
	EXPECT_THROW(legacy.oser & add, std::runtime_error);
	EXPECT_TRUE(legacy.extractBuffer().empty())
		<< "The ADD pack must reject unsupported metadata before writing its header";

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.oser & add;
	CMemorySerializer reader(current.extractBuffer());
	reader.iser.version = ESerializationVersion::CURRENT;
	BattleUnitsChanged received;
	reader.iser & received;
	ASSERT_EQ(received.changedStacks.size(), 1u);
	EXPECT_EQ(received.changedStacks.front().data["rebirthOriginalAggregateHP"].Integer(), 9007199254740993LL);

	info.rebirthOriginalAggregateHP = 0;
	JsonNode legacyData;
	info.save(legacyData);
	battle::UnitInfo restoredLegacy;
	restoredLegacy.load(info.id, legacyData);
	EXPECT_EQ(restoredLegacy.rebirthOriginalAggregateHP, 0)
		<< "Older ADD payloads that omit the key retain the zero default";
}

TEST_F(NewHorizonsElementalRebirthTest, UnitInfoRejectsMalformedRebirthOutputMetadata)
{
	battle::UnitInfo info;
	info.id = 99;
	info.count = 1;
	info.type = creature("core:fireElemental");
	info.side = BattleSide::DEFENDER;
	info.position = BattleHex(0);
	info.summoned = true;
	info.rebirthOriginalAggregateHP = 100;
	JsonNode data;

	info.rebirthOriginalAggregateHP = -1;
	EXPECT_THROW(info.save(data), std::runtime_error);
	info.rebirthOriginalAggregateHP = 100;
	info.summoned = false;
	EXPECT_THROW(info.save(data), std::runtime_error);
	info.summoned = true;
	info.natureSummoned = true;
	EXPECT_THROW(info.save(data), std::runtime_error);
	info.natureSummoned = false;
	info.count = 0;
	EXPECT_THROW(info.save(data), std::runtime_error);
}

TEST_F(NewHorizonsElementalRebirthTest, InvalidRebirthOutputADDDoesNotMutateBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC));
	const auto originalStackCount = battle()->battleGetAllUnits(false).size();

	battle::UnitInfo info;
	info.id = battle()->battleNextUnitId();
	info.count = 1;
	info.type = creature("core:fireElemental");
	info.side = BattleSide::DEFENDER;
	info.position = BattleHex(0);
	info.summoned = true;
	info.rebirthOriginalAggregateHP = std::numeric_limits<int64_t>::max();
	JsonNode data;
	info.save(data);

	EXPECT_THROW(battle()->addUnit(info.id, data), std::runtime_error);
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), originalStackCount)
		<< "Invalid Rebirth output metadata must be rejected before publishing a stack";
}

TEST_F(NewHorizonsElementalRebirthTest, IndependentSpellReductionLeavesUnbonusedStackUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 7, "core:pikeman", true));
	auto * protectedUnit = stackAt(BattleSide::DEFENDER, SlotID(0));
	auto * control = stackAt(BattleSide::DEFENDER, SlotID(1));
	ASSERT_NE(protectedUnit, nullptr);
	ASSERT_NE(control, nullptr);

	const Bonus ward(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSource::OTHER, 2000, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
	SetStackEffect applyWard;
	applyWard.battleID = BattleID(0);
	applyWard.toAdd.emplace_back(protectedUnit->unitId(), std::vector<Bonus>{ward});
	gameHandler->sendAndApply(applyWard);

	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);

	EXPECT_EQ(mechanics->adjustEffectValue(control), rawDamage);
	EXPECT_EQ(mechanics->adjustEffectValue(protectedUnit), rawDamage * 80 / 100)
		<< "The existing spell receiver path must consume Ward as an independent 20% magical reduction";
}
