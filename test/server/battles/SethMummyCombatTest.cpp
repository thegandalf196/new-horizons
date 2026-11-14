/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/AttackAbilityQueries.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/NewHorizonsRecruitmentGrowth.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/AbilityCaster.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
class SethMummyCombatTest : public BattleTestFixture
{
protected:
	CStack * actor = nullptr;
	CStack * target = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons Mummy content";
	}

	void prepare(bool royal, bool ranged, bool siege = false)
	{
		startGame(siege);
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName(
			royal ? "new-horizons:royalMummy" : "core:mummy"), 5));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 500));
		setTestSpellPointTotal(attackerSideHero, 20);
		setTestSpellPointTotal(defenderSideHero, 20);
		if(siege)
		{
			const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
			ASSERT_EQ(towns.size(), 1u);
			ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
			startBattle(towns.front());
		}
		else
			startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? actor : target) = battle()->getStack(unit->unitId());
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(target, nullptr);
		actor->setPosition(BattleHex(3, 5));
		target->setPosition(BattleHex(siege ? 15 : ranged ? 6 : 4, 5));
		for(auto * unit : {actor, target})
			for(const auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
					type, BonusSource::OTHER, 1, BonusSourceID()));
		blockRetaliation(actor);
		forceMaximumDamage(actor);
		beginCombat();
		activateActor();
		server.casts.clear();
		server.attacks.clear();
		server.startedActions.clear();
	}

	void activateActor()
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = actor->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	bool strike(bool ranged)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), ranged
			? BattleAction::makeShotAttack(actor, target)
			: BattleAction::makeMeleeAttack(actor, target, actor->getPosition()));
	}

	// Select a real biased ability-stream state with the desired next 25% roll.
	// Damage, immunity, proc admission and status application still run normally.
	void nextCurseRoll(bool success)
	{
		GameRandomizer search(*gameState());
		CMemorySerializer original;
		gameHandler->randomizer->serialize(original.oser);
		search.serialize(original.iser);
		const auto owner = battle()->getSideArmy(actor->unitSide())->id;
		search.rollCombatAbility(owner, 100); // Allocate this independent stream.
		for(int attempt = 0; attempt < 100; ++attempt)
		{
			CMemorySerializer before;
			search.serialize(before.oser);
			if(search.rollCombatAbility(owner, 25) == success)
			{
				gameHandler->randomizer->serialize(before.iser);
				return;
			}
		}
		FAIL() << "Could not select the requested deterministic Curse roll";
	}

	void expectOrdinaryAttack(bool ranged)
	{
		ASSERT_EQ(server.attacks.size(), 1u);
		EXPECT_EQ(server.attacks.front().stackAttacking, actor->unitId());
		EXPECT_EQ(server.attacks.front().shot(), ranged);
		EXPECT_FALSE(server.attacks.front().counter());
		ASSERT_EQ(server.startedActions.size(), 1u);
		EXPECT_EQ(server.startedActions.front().ba.actionType,
			ranged ? EActionType::SHOOT : EActionType::WALK_AND_ATTACK);
	}
};

TEST_F(SethMummyCombatTest, LoadedUpgradePreservesOrdinaryMummyStatsAndCanonicalEconomics)
{
	startGame();
	const auto * base = creatureByName("core:mummy").toCreature();
	const auto * royal = creatureByName("new-horizons:royalMummy").toCreature();
	ASSERT_NE(base, nullptr);
	ASSERT_NE(royal, nullptr);
	EXPECT_TRUE(base->upgrades.contains(royal->getId()));
	EXPECT_EQ(base->getIndex(), 141);
	EXPECT_EQ(base->getBaseHitPoints(), 30);
	EXPECT_EQ(base->getBaseAttack(), 7);
	EXPECT_EQ(base->getBaseDefense(), 7);
	EXPECT_EQ(base->getBaseDamageMin(), 3);
	EXPECT_EQ(base->getBaseDamageMax(), 5);
	EXPECT_EQ(base->getBaseSpeed(), 5);
	EXPECT_EQ(base->getBaseInitiative(), 5);
	for(const auto getter : {&CCreature::getBaseHitPoints, &CCreature::getBaseAttack,
		&CCreature::getBaseDefense, &CCreature::getBaseDamageMin, &CCreature::getBaseDamageMax,
		&CCreature::getBaseSpeed, &CCreature::getBaseInitiative, &CCreature::getFightValue,
		&CCreature::getAIValue, &CCreature::getHorde, &CCreature::getAdvMapAmountMin,
		&CCreature::getAdvMapAmountMax})
		EXPECT_EQ((base->*getter)(), (royal->*getter)());
	EXPECT_EQ(base->getFactionID(), royal->getFactionID());
	EXPECT_EQ(base->isDoubleWide(), royal->isDoubleWide());
	EXPECT_EQ(base->getLevel(), royal->getLevel());
	EXPECT_EQ(base->getBaseShots(), 0);
	EXPECT_EQ(royal->getBaseShots(), 12);
	EXPECT_FALSE(base->hasBonusOfType(BonusType::SHOOTER));
	EXPECT_TRUE(royal->hasBonusOfType(BonusType::SHOOTER));
	EXPECT_TRUE(royal->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
	for(const auto * unit : {base, royal})
	{
		EXPECT_TRUE(unit->hasBonusOfType(BonusType::UNDEAD));
		EXPECT_FALSE(unit->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
		EXPECT_FALSE(unit->hasBonusOfType(BonusType::NO_DISTANCE_PENALTY));
		EXPECT_FALSE(unit->hasBonusOfType(BonusType::NO_WALL_PENALTY));
	}
	EXPECT_EQ(base->getFullRecruitCost(), creatureByName("core:wight").toCreature()->getFullRecruitCost());
	EXPECT_EQ(royal->getFullRecruitCost(), creatureByName("core:wraith").toCreature()->getFullRecruitCost());
	EXPECT_EQ(base->getRecruitCost(EGameResID::GOLD), 200);
	EXPECT_EQ(royal->getRecruitCost(EGameResID::GOLD), 230);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(gameState()->getSettings()), 50);
	for(const auto * name : {"core:mummy", "new-horizons:royalMummy", "core:wight", "core:wraith"})
	{
		const auto id = creatureByName(name);
		EXPECT_EQ(gameState()->getCreatureBaseGrowth(id), 7) << name;
		EXPECT_EQ(id.toCreature()->getGrowth(), 7) << name;
		EXPECT_EQ(newHorizonsRecruitmentGrowth::generatedAmount(
			gameState()->getCreatureBaseGrowth(id), gameState()->getSettings()), 4u) << name;
	}
}

class SethMummyCurseTest : public SethMummyCombatTest, public ::testing::WithParamInterface<int>
{
protected:
	void resolve(bool succeeds, bool immune = false)
	{
		const bool ranged = GetParam() == 2;
		ASSERT_NO_FATAL_FAILURE(prepare(GetParam() != 0, ranged));
		const auto ability = attackAbilityQueries::parameters(*actor, BonusType::SPELL_AFTER_ATTACK, SpellID::CURSE, ranged);
		EXPECT_TRUE(ability.castMe);
		EXPECT_EQ(ability.chance, 25);
		EXPECT_EQ(ability.spellLevel, 0);
		if(immune)
			target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::CURSE))));
		ASSERT_NO_FATAL_FAILURE(nextCurseRoll(succeeds));
		const auto actorAllowance = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
		const auto enemyAllowance = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
		const auto actorMana = attackerSideHero->getManaAvailable();
		const auto enemyMana = defenderSideHero->getManaAvailable();
		const auto shots = actor->shots.available();
		const auto health = target->getAvailableHealth();
		const BattleAttackInfo attackInfo(actor, target, 0, ranged);
		EXPECT_TRUE(attackInfo.physicalDamage);
		const auto expectedDamage = battle()->calculateDmgRange(attackInfo).damage.max;
		spells::AbilityCaster caster(actor, ability.spellLevel);
		spells::BattleCast cast(battle(), &caster, spells::Mode::PASSIVE, SpellID(SpellID::CURSE).toSpell());
		const int expectedDuration = cast.getSpell()->battleMechanics(&cast)->getEffectDuration();
		EXPECT_EQ(expectedDuration, 3);
		ASSERT_TRUE(strike(ranged));
		expectOrdinaryAttack(ranged);
		EXPECT_EQ(health - target->getAvailableHealth(), expectedDamage);
		EXPECT_EQ(actor->shots.available(), shots - static_cast<int>(ranged));
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), actorAllowance);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), enemyAllowance);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), actorMana);
		EXPECT_EQ(defenderSideHero->getManaAvailable(), enemyMana);
		const auto curse = target->getFirstBonus(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::CURSE))).And(Selector::type()(BonusType::ALWAYS_MINIMUM_DAMAGE)));
		const auto announcements = server.castsOf(SpellID::CURSE);
		if(succeeds && !immune)
		{
			ASSERT_NE(curse, nullptr);
			EXPECT_EQ(curse->val, 0);
			EXPECT_EQ(curse->turnsRemain, expectedDuration);
			ASSERT_EQ(announcements.size(), 1u);
			EXPECT_FALSE(announcements.front().announcement.castByHero);
			EXPECT_FALSE(announcements.front().announcement.activeCast);
			EXPECT_EQ(announcements.front().announcement.casterStack, actor->unitId());
			EXPECT_EQ(announcements.front().announcement.paidHeroManaCost, 0);
		}
		else
		{
			EXPECT_EQ(curse, nullptr);
			EXPECT_TRUE(announcements.empty());
		}
	}
};

TEST_P(SethMummyCurseTest, SuccessfulNativeProcPreservesDurationAndHeroResources) { resolve(true); }
TEST_P(SethMummyCurseTest, FailedNativeRollDoesNotCastCurse) { resolve(false); }
TEST_P(SethMummyCurseTest, SpecificCurseImmunityPreventsTheNativeProc) { resolve(true, true); }
INSTANTIATE_TEST_SUITE_P(MummyMeleeRoyalMeleeRoyalShot, SethMummyCurseTest, ::testing::Values(0, 1, 2));

TEST_F(SethMummyCombatTest, RoyalMeleeUsesFullDamageAndDoesNotSpendAmmunition)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false));
	const auto melee = battle()->calculateDmgRange(BattleAttackInfo(actor, target, 0, false));
	EXPECT_FALSE(battle()->battleCanShootAction(actor, target->getPosition()));
	const auto adjacentPosition = target->getPosition();
	target->setPosition(BattleHex(6, 5));
	ASSERT_TRUE(battle()->battleCanShootAction(actor, target->getPosition()));
	ASSERT_FALSE(battle()->battleHasDistancePenalty(actor, actor->getPosition(), target->getPosition()));
	ASSERT_FALSE(battle()->battleHasWallPenalty(actor, actor->getPosition(), target->getPosition()));
	const auto shot = battle()->calculateDmgRange(BattleAttackInfo(actor, target, 0, true));
	EXPECT_EQ(melee.damage.max, shot.damage.max);
	target->setPosition(adjacentPosition);
	EXPECT_FALSE(battle()->battleCanShootAction(actor, target->getPosition()));
	const int shots = actor->shots.available();
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(strike(false));
	EXPECT_EQ(health - target->getAvailableHealth(), melee.damage.max);
	EXPECT_EQ(actor->shots.available(), shots);
	expectOrdinaryAttack(false);
}

TEST_F(SethMummyCombatTest, RoyalNormalBlockingAndAmmoRejectWithoutDamageOrHeroPayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	ASSERT_TRUE(battle()->battleCanShootAction(actor, target->getPosition()));
	EXPECT_FALSE(battle()->battleHasDistancePenalty(actor, actor->getPosition(), target->getPosition()));
	EXPECT_TRUE(battle()->battleHasDistancePenalty(actor, actor->getPosition(), BattleHex(15, 5)));
	auto * blocker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(4, 5), 100);
	const auto health = target->getAvailableHealth();
	const auto allowance = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_FALSE(battle()->battleCanShootAction(actor, target->getPosition()));
	EXPECT_FALSE(strike(true));
	EXPECT_EQ(actor->shots.available(), 12);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(blocker->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	ASSERT_TRUE(battle()->battleCanShootAction(actor, target->getPosition()));
	actor->shots.use(12);
	EXPECT_FALSE(battle()->battleCanShootAction(actor, target->getPosition()));
	EXPECT_FALSE(strike(true));
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowance);
	EXPECT_TRUE(server.attacks.empty());
}

TEST_F(SethMummyCombatTest, RoyalSiegeShotKeepsOrdinaryDistanceAndWallPenalties)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true, true));
	ASSERT_TRUE(battle()->battleCanShootAction(actor, target->getPosition()));
	EXPECT_TRUE(battle()->battleHasDistancePenalty(actor, actor->getPosition(), target->getPosition()));
	EXPECT_TRUE(battle()->battleHasWallPenalty(actor, actor->getPosition(), target->getPosition()));
	const auto damage = battle()->calculateDmgRange(BattleAttackInfo(actor, target, 0, true)).damage.max;
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(strike(true));
	EXPECT_EQ(health - target->getAvailableHealth(), damage);
	EXPECT_EQ(actor->shots.available(), 11);
	expectOrdinaryAttack(true);
}
}
