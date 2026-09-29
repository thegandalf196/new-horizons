/*
 * NewHorizonsGuardianSpiritTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
class NewHorizonsGuardianSpiritTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void startGuardianBattle()
	{
		startGame();
		startBattle();
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(leftHex), 1000);
	}

	void grantGuardianSpirit(CStack * stack, int32_t pool, int16_t rounds = 2)
	{
		Bonus marker(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
			BonusSource::SPELL_EFFECT, pool, BonusSourceID(SpellID(SpellID::HASTE)));
		marker.turnsRemain = rounds;

		SetStackEffect effects;
		effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{marker});
		gameHandler->sendAndApply(effects);
	}

	BattleStackAttacked hitWith(CStack * stack, int64_t damage, battle::DamageProvenance provenance)
	{
		BattleStackAttacked hit;
		hit.stackAttacked = stack->unitId();
		hit.damageAmount = damage;
		auto state = stack->acquireState();
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state,
			false, false, provenance);
		StacksInjured injury;
		injury.battleID = BattleID(0);
		injury.stacks.push_back(hit);
		gameHandler->sendAndApply(injury);
		return hit;
	}

	CStack * target = nullptr;
};
}

TEST_F(NewHorizonsGuardianSpiritTest, AuthoritativeHeroCastSetsPoolFromSpellPower)
{
	const SpellID spell(SpellID::decode("new-horizons:guardianSpirit"));
	ASSERT_NE(spell, SpellID::NONE);

	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	startBattle();

	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	beginCombat();

	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto markers = target->getBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT));
	ASSERT_TRUE(markers);
	ASSERT_EQ(markers->size(), 1u);
	EXPECT_EQ(markers->front()->source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(markers->front()->sid.as<SpellID>(), spell);
	EXPECT_EQ(markers->front()->val, 90); // 50 base + 2 x 20 Spell Power at 100% coefficient.
	EXPECT_EQ(target->guardianSpiritHitPoints, 90);
	EXPECT_EQ(target->guardianSpiritRoundsRemaining, 2);
}

TEST_F(NewHorizonsGuardianSpiritTest, OnlyTypedPhysicalDamageConsumesSavedPoolAndExhaustionRemovesMarker)
{
	startGuardianBattle();
	grantGuardianSpirit(target, 100);
	const auto initialHealth = target->getAvailableHealth();

	const auto fullyAbsorbed = hitWith(target, 40, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(fullyAbsorbed.damageAmount, 0);
	EXPECT_EQ(target->guardianSpiritHitPoints, 60);
	EXPECT_EQ(target->guardianSpiritRoundsRemaining, 2);
	EXPECT_EQ(target->getAvailableHealth(), initialHealth);

	const auto savedState = target->save();
	auto restored = target->acquireState();
	restored->load(savedState);
	EXPECT_EQ(restored->guardianSpiritHitPoints, 60);
	EXPECT_EQ(restored->guardianSpiritRoundsRemaining, 2);
	auto copied = target->acquireState();
	*copied = *restored;
	EXPECT_EQ(copied->guardianSpiritHitPoints, 60);
	EXPECT_EQ(copied->guardianSpiritRoundsRemaining, 2);

	const auto spellHit = hitWith(target, 30, battle::DamageProvenance::SPELL);
	EXPECT_EQ(spellHit.damageAmount, 30);
	EXPECT_EQ(target->guardianSpiritHitPoints, 60);
	EXPECT_EQ(target->getAvailableHealth(), initialHealth - 30);

	const auto overflowHit = hitWith(target, 70, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(overflowHit.damageAmount, 10);
	EXPECT_NE(overflowHit.flags & BattleStackAttacked::GUARDIAN_SPIRIT_EXHAUSTED, 0u);
	EXPECT_EQ(target->guardianSpiritHitPoints, 0);
	EXPECT_EQ(target->guardianSpiritRoundsRemaining, 0);
	EXPECT_EQ(target->getAvailableHealth(), initialHealth - 40);
	EXPECT_FALSE(target->hasBonus(Selector::type()(BonusType::GUARDIAN_SPIRIT)));
}

TEST_F(NewHorizonsGuardianSpiritTest, TimedMarkerControlsExpiryAndPhysicalAttackReportsAbsorption)
{
	startGuardianBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(rightHex), 2);
	grantGuardianSpirit(target, 1);
	blockRetaliation(attacker);
	beginCombat();

	server.battleLogLines.clear();
	const auto healthBeforeAttack = target->getAvailableHealth();
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	EXPECT_EQ(target->guardianSpiritHitPoints, 0);
	EXPECT_EQ(target->guardianSpiritRoundsRemaining, 0);
	EXPECT_FALSE(target->hasBonus(Selector::type()(BonusType::GUARDIAN_SPIRIT)));
	EXPECT_LT(target->getAvailableHealth(), healthBeforeAttack);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Guardian Spirit absorbs 1 physical damage") != std::string::npos
			&& line.find("damage passes through") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);

	grantGuardianSpirit(target, 50);
	ASSERT_EQ(target->guardianSpiritRoundsRemaining, 2);
	battle()->nextRound();
	EXPECT_EQ(target->guardianSpiritRoundsRemaining, 1);
	EXPECT_EQ(target->guardianSpiritHitPoints, 50);
	battle()->nextRound();
	EXPECT_EQ(target->guardianSpiritRoundsRemaining, 0);
	EXPECT_EQ(target->guardianSpiritHitPoints, 0);
	EXPECT_FALSE(target->hasBonus(Selector::type()(BonusType::GUARDIAN_SPIRIT)));
}
