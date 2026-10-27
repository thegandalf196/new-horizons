/*
 * NewHorizonsLivingArmorTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../../lib/battle/UnitAdditionRules.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/IncomingMagicalDamage.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
SpellID summonSpell() { return SpellID(SpellID::decode("new-horizons:summonLivingArmor")); }

class NewHorizonsLivingArmorTest : public BattleTestFixture
{
protected:
	const BattleHex destination{8, 5};
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(summonSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 21, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		beginCombat();
	}
	bool cast(BattleHex hex)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = summonSpell();
		action.aimToHex(hex);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	CStack * summoned() const
	{
		for(const auto & stack : battle()->stacks)
			if(stack->isSummoned() && stack->unitType()->getJsonKey() == "new-horizons:livingArmor")
				return stack.get();
		return nullptr;
	}
	void advanceRound()
	{
		BattleNextRound round;
		round.battleID = BattleID(0);
		gameHandler->sendAndApply(round);
	}
};
}

TEST_F(NewHorizonsLivingArmorTest, SelectedSummonMatchesExactHealthPreviewAndThreeRoundLifetime)
{
	prepare();
	const auto preview = battle()->getSpellEffectValue(summonSpell().toSpell(), attackerSideHero,
		spells::Mode::HERO, destination);
	ASSERT_NE(preview, nullptr);
	EXPECT_EQ(preview->hpDelta, 61);
	EXPECT_EQ(preview->unitsDelta, 7);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(cast(destination));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 5);
	auto * unit = summoned();
	ASSERT_NE(unit, nullptr);
	EXPECT_EQ(unit->getPosition(), destination);
	EXPECT_EQ(unit->getAvailableHealth(), 61);
	EXPECT_EQ(unit->getCount(), 7);
	EXPECT_EQ(unit->summonRoundsRemaining, 3);
	EXPECT_FALSE(unit->natureSummoned);
	EXPECT_FALSE(unit->isClone());
	EXPECT_FALSE(unit->isFrozen());
	EXPECT_TRUE(unit->hasBonusOfType(BonusType::NON_LIVING));
	EXPECT_FALSE(unit->hasBonusOfType(BonusType::UNDEAD));
	advanceRound();
	EXPECT_EQ(unit->summonRoundsRemaining, 2);
	advanceRound();
	EXPECT_TRUE(unit->alive());
	EXPECT_EQ(unit->summonRoundsRemaining, 1);
	advanceRound();
	EXPECT_FALSE(unit->alive());
	EXPECT_EQ(unit->summonRoundsRemaining, 0);
}

TEST_F(NewHorizonsLivingArmorTest, OccupiedHexRejectsWithoutManaOrActionSpendAndRecastsStayIndependent)
{
	prepare();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto count = battle()->stacks.size();
	ASSERT_FALSE(cast(BattleHex(leftHex)));
	EXPECT_EQ(mana, attackerSideHero->getManaAvailable());
	EXPECT_EQ(count, battle()->stacks.size());
	ASSERT_TRUE(cast(destination));
	auto * first = summoned();
	ASSERT_NE(first, nullptr);
	advanceRound();
	ASSERT_TRUE(cast(BattleHex(9, 5)));
	EXPECT_EQ(first->summonRoundsRemaining, 2);
	int independent = 0;
	for(const auto & stack : battle()->stacks)
		if(stack->isSummoned() && stack->unitType()->getJsonKey() == "new-horizons:livingArmor")
		{
			++independent;
			if(stack.get() != first) EXPECT_EQ(stack->summonRoundsRemaining, 3);
		}
	EXPECT_EQ(independent, 2);
}

TEST_F(NewHorizonsLivingArmorTest, JsonAndDetachedProjectionPreserveLifetimeWithoutMutatingSource)
{
	prepare();
	ASSERT_TRUE(cast(destination));
	auto * unit = summoned();
	ASSERT_NE(unit, nullptr);
	unit->afterNewRound();
	const auto snapshot = unit->save();
	{
		battle::ProjectedBattleWorldArena projection(*battle());
		const auto * projected = projection.findUnit(unit->unitId());
		ASSERT_NE(projected, nullptr);
		EXPECT_EQ(projected->summonRoundsRemaining, 2);
		EXPECT_EQ(unit->save(), snapshot);
	}
	auto copy = std::dynamic_pointer_cast<battle::CUnitState>(unit->acquireState()->acquire());
	ASSERT_NE(copy, nullptr);
	copy->load(snapshot);
	EXPECT_EQ(copy->summonRoundsRemaining, 2);
	copy->afterNewRound();
	EXPECT_EQ(copy->summonRoundsRemaining, 1);
	EXPECT_EQ(unit->summonRoundsRemaining, 2);
}

TEST_F(NewHorizonsLivingArmorTest, MagicalVulnerabilityIncludesUntaggedMagicAndExcludesPhysicalAbilities)
{
	prepare();
	ASSERT_TRUE(cast(destination));
	auto * target = summoned();
	ASSERT_NE(target, nullptr);
	for(const auto id : {SpellID::MAGIC_ARROW, SpellID::FIREBALL})
		EXPECT_EQ(SpellID(id).toSpell()->adjustRawDamage(nullptr, target, 20, 0, 0, 100,
			true, true, false), 40);
	const auto * physical = SpellID(SpellID::DEATH_CLOUD).toSpell();
	ASSERT_FALSE(physical->isMagical());
	EXPECT_EQ(physical->adjustRawDamage(nullptr, target, 20, 0, 0, 100,
		true, true, false), 20);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(SpellID(SpellID::MAGIC_ARROW).toSpell()->adjustRawDamage(nullptr, target, 20,
		0, 0, 100, true, true, false), 20);
}

TEST(LivingArmorSerialization, OlderWritersRejectTimedSpawnAndOuterPacketBeforePrefixes)
{
	UnitChanges change(1, UnitChanges::EOperation::ADD);
	change.data["summonRoundsRemaining"].Integer() = 3;
	CMemorySerializer direct;
	direct.oser.version = ESerializationVersion::NEW_HORIZONS_NECROMANCY_POLICY_V2;
	EXPECT_THROW(change.serialize(direct.oser), std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	BattleUnitsChanged envelope;
	envelope.changedStacks.push_back(change);
	CMemorySerializer outer;
	outer.oser.version = direct.oser.version;
	EXPECT_THROW(envelope.serialize(outer.oser), std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
	CMemorySerializer current;
	ASSERT_NO_THROW(envelope.serialize(current.oser));
	BattleUnitsChanged restored;
	ASSERT_NO_THROW(restored.serialize(current.iser));
	ASSERT_EQ(restored.changedStacks.size(), 1u);
	EXPECT_EQ(restored.changedStacks.front().data["summonRoundsRemaining"].Integer(), 3);
}

TEST(LivingArmorSerialization, AllMagicalVulnerabilityRejectsOldBonusAndEffectPrefixes)
{
	Bonus value(BonusDuration::PERMANENT, BonusType::MORE_DAMAGE_FROM_SPELL,
		BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID());
	CMemorySerializer direct;
	direct.oser.version = ESerializationVersion::NEW_HORIZONS_NECROMANCY_POLICY_V2;
	EXPECT_THROW(value.serialize(direct.oser), std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	SetStackEffect packet;
	packet.toAdd.emplace_back(1, std::vector<Bonus>{value});
	CMemorySerializer outer;
	outer.oser.version = direct.oser.version;
	EXPECT_THROW(packet.serialize(outer.oser), std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
}

TEST_F(NewHorizonsLivingArmorTest, TimeStopPausesCountdownAndPhysicalReductionUsesOrdinaryDamageLayer)
{
	prepare();
	ASSERT_TRUE(cast(destination));
	auto * target = summoned();
	ASSERT_NE(target, nullptr);
	auto stasis = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	target->addNewBonus(stasis);
	target->afterNewRound();
	EXPECT_EQ(target->summonRoundsRemaining, 3);
	target->removeBonus(stasis);
	target->afterNewRound();
	EXPECT_EQ(target->summonRoundsRemaining, 2);
	auto * attacker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 5), 200);
	auto * ordinary = addStack(BattleSide::ATTACKER, creatureByName("core:skeletonWarrior"), BattleHex(9, 5), 7);
	// Compare the same creature defense to isolate the later physical layer.
	const int difference = target->getDefense(false) - ordinary->getDefense(false);
	ordinary->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, difference, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));
	const auto reduced = battle()->calculateDmgRange(BattleAttackInfo(attacker, target, 0, false)).damage;
	const auto full = battle()->calculateDmgRange(BattleAttackInfo(attacker, ordinary, 0, false)).damage;
	EXPECT_LE(std::abs(reduced.max * 4 - full.max), 4);
	EXPECT_LE(std::abs(reduced.min * 4 - full.min), 4);
}

TEST(LivingArmorSerialization, LegacySpawnDefaultsToCombatLongAndInvalidLifetimeFailsClosed)
{
	battle::UnitInfo value;
	value.type = CreatureID(0);
	value.count = 1;
	value.side = BattleSide::ATTACKER;
	value.position = BattleHex(8, 5);
	JsonNode data;
	value.save(data);
	data.Struct().erase("summonRoundsRemaining");
	battle::UnitInfo restored;
	restored.summonRoundsRemaining = 9;
	ASSERT_NO_THROW(restored.load(0, data));
	EXPECT_EQ(restored.summonRoundsRemaining, 0);
	data["summonRoundsRemaining"].Integer() = -1;
	EXPECT_THROW(restored.load(0, data), std::runtime_error);
	data["summonRoundsRemaining"].Integer() = 3;
	data["summoned"].Bool() = false;
	EXPECT_THROW(restored.load(0, data), std::runtime_error);
}

TEST_F(NewHorizonsLivingArmorTest, MagicalCreatureShotUsesVulnerabilityOnceAndKeepsPhysicalReductionSeparate)
{
	prepare();
	ASSERT_TRUE(cast(destination));
	auto * target = summoned();
	ASSERT_NE(target, nullptr);
	auto * control = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(9, 5), 7);
	auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:lich"), BattleHex(12, 5), 1);
	ASSERT_NE(control, nullptr);
	ASSERT_NE(shooter, nullptr);
	ASSERT_EQ(control->getDefense(true), target->getDefense(true));
	const BattleAttackInfo magical(shooter, target, 0, true);
	ASSERT_FALSE(magical.physicalDamage);
	const auto plain = battle()->calculateDmgRange(BattleAttackInfo(shooter, control, 0, true));
	const auto vulnerable = battle()->calculateDmgRange(magical);
	ASSERT_GT(plain.damage.min, 0);
	EXPECT_EQ(vulnerable.damage.min, 2 * plain.damage.min);
	EXPECT_EQ(vulnerable.damage.max, 2 * plain.damage.max);
	EXPECT_EQ(vulnerable.damageBeforeDefense.min, plain.damageBeforeDefense.min);
	EXPECT_EQ(vulnerable.damageBeforeDefense.max, plain.damageBeforeDefense.max);
	EXPECT_GE(vulnerable.kills.max, plain.kills.max);

	const auto physicalBefore = battle()->calculateDmgRange(BattleAttackInfo(shooter, control, 0, false));
	control->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MORE_DAMAGE_FROM_SPELL,
		BonusSource::OTHER, 100, BonusSourceID()));
	const auto physicalAfter = battle()->calculateDmgRange(BattleAttackInfo(shooter, control, 0, false));
	EXPECT_EQ(physicalAfter.damage.min, physicalBefore.damage.min);
	EXPECT_EQ(physicalAfter.damage.max, physicalBefore.damage.max);
	EXPECT_EQ(physicalAfter.kills.max, physicalBefore.kills.max);

	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::DAMAGE_RECEIVED_CAP,
		BonusSource::OTHER, 20, BonusSourceID()));
	const auto capped = battle()->calculateDmgRange(magical);
	EXPECT_EQ(capped.damage.min, 2);
	EXPECT_EQ(capped.damage.max, 2);
	EXPECT_EQ(capped.kills.max, 1);
}

TEST_F(NewHorizonsLivingArmorTest, RawArrivalAndMagicalAbilitiesShareTheSameOneTimeVulnerability)
{
	prepare();
	ASSERT_TRUE(cast(destination));
	auto * target = summoned();
	ASSERT_NE(target, nullptr);
	// Hellfire uses this generic raw-magical entrypoint directly. Current
	// non-spell abilities use their own saved mitigation then the same factor.
	EXPECT_EQ(spells::adjustUnmitigatedMagicalDamage(*target, 40), 80);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *target, 40), 80);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	ASSERT_TRUE(battle()->battleUsesNewHorizonsMultiplicativeMDR());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *target, 40), 40);
	EXPECT_EQ(spells::adjustUnmitigatedMagicalDamage(*target, 40), 80);
}

TEST_F(NewHorizonsLivingArmorTest, NegativeAllMagicalMarkerIsIdentityWhileExactSpellModifierKeepsSignedMeaning)
{
	prepare();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), destination, 7);
	ASSERT_NE(target, nullptr);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MORE_DAMAGE_FROM_SPELL,
		BonusSource::OTHER, -50, BonusSourceID()));
	EXPECT_EQ(spells::incomingMagicalDamagePercent(*target), 0);
	EXPECT_EQ(spells::adjustUnmitigatedMagicalDamage(*target, 40), 40);
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	EXPECT_EQ(arrow->adjustRawDamage(nullptr, target, 40, 0, 0, 100, true, true, false), 40);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MORE_DAMAGE_FROM_SPELL,
		BonusSource::OTHER, -50, BonusSourceID(), BonusSubtypeID(arrow->getId())));
	EXPECT_EQ(arrow->adjustRawDamage(nullptr, target, 40, 0, 0, 100, true, true, false), 20);
}
