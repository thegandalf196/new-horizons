/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../SylvanTacticsHistoricalRules.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsSylvanTactics.h"
#include "../../../lib/battle/NewHorizonsSylvanTacticsRevision.h"
#include "../../../lib/battle/NewHorizonsSylvanTacticsV2.h"
#include "../../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleActionProcessor.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
namespace revision = newHorizonsSylvanTacticsRevision;
class SylvanTactics41Test : public BattleTestFixture
{
protected:
	virtual int policy() const { return 4; }
	void SetUp() override { BattleTestFixture::SetUp(); startGame(); }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode rules = sylvanTacticsHistoricalRules(policy());
		// A policy-3 capture keeps its own payload, never the installed 4.1 value.
		if(policy() == 3)
			for(auto & row : rules["skills"]["new-horizons:sylvanLuck"]["perks"].Vector())
				if(row["id"].String().ends_with(".elvenPrecision"))
					row["effect"]["parameters"]["attackDamagePercent"].Integer() = 25;
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
	}
	/// One perk per Sylvan Tactics rank, in acquisition order.
	void perks(std::initializer_list<const char *> suffixes)
	{
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
		attackerSideHero->setSecSkillLevel(skill, static_cast<int>(suffixes.size()), ChangeValueMode::ABSOLUTE);
		for(const auto * suffix : suffixes)
		{
			const auto id = std::string("new-horizons:sylvanLuck.") + suffix;
			attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", id});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:sylvanLuck", id)) << id;
		}
	}
	/// Exact damage: no fractional Leadership factor on either side.
	void exactLeadership()
	{
		for(auto * hero : {attackerSideHero, defenderSideHero})
			hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::LEADERSHIP, BonusSource::OTHER, 100000, BonusSourceID()));
	}
	CStack * stack(const char * creature, BattleSide side, BattleHex hex, int count = 10)
	{
		return addStack(side, creatureByName(creature), hex, count);
	}
	int64_t damage(const CStack * actor, const CStack * target, bool ranged = false, bool retaliation = false)
	{
		BattleAttackInfo info(actor, target, 0, ranged);
		info.retaliation = retaliation;
		return battle()->calculateDmgRange(info).damage.min;
	}
	bool act(const CStack * actor, const BattleAction & action)
	{
		battle()->activeStack = actor->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(actor->unitSide()), action);
	}
	int dappledHexes(const CStack * unit) const
	{
		const auto markers = unit->getUnstackedBonuses(CSelector(revision::isDappledMovement));
		return markers->empty() ? 0 : markers->front()->val;
	}
	int redeployments(const CStack * unit) const
	{
		return static_cast<int>(unit->getUnstackedBonuses(CSelector(revision::isQuickRedeployment))->size());
	}
	void apply(const newHorizonsOrdinaryStrike::EffectPlan & plan)
	{
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		if(!plan.remove.empty()) effect.toRemove.emplace_back(plan.target, plan.remove);
		if(!plan.add.empty()) effect.toAdd.emplace_back(plan.target, plan.add);
		gameHandler->sendAndApply(effect);
	}
};
class SylvanTactics41Policy3Test : public SylvanTactics41Test
{
protected:
	int policy() const override { return 3; }
};

TEST_F(SylvanTactics41Test, PolicyFourKeepsTheMixedRosterButNoWaitingOrDefendHistory)
{
	const auto & rules = attackerSideHero->getPerkState().getRules();
	EXPECT_TRUE(newHorizonsHeroes::usesSylvanTacticsV3(rules));
	EXPECT_TRUE(newHorizonsHeroes::usesSylvanTacticsV4(rules));
	EXPECT_FALSE(newHorizonsHeroes::usesSylvanTacticsV2(rules));
	ASSERT_NO_FATAL_FAILURE(perks({"luckyRecovery", "natureSProvidence"}));
	SylvanLuckState fortune;
	newHorizonsSylvanTactics::configure(*attackerSideHero, fortune);
	EXPECT_TRUE(fortune.tactics.enabled);
	EXPECT_FALSE(fortune.tactics.dappledCover);
	EXPECT_FALSE(fortune.tactics.quickRedeployment);
	EXPECT_TRUE(revision::active(attackerSideHero, "luckyRecovery"));
	EXPECT_TRUE(revision::active(attackerSideHero, "natureSProvidence"));
	EXPECT_FALSE(revision::active(attackerSideHero, "elvenPrecision"));
}

TEST_F(SylvanTactics41Test, DappledCoverNeedsThreeVoluntaryHexesAndEndsAtTheNextActivation)
{
	ASSERT_NO_FATAL_FAILURE(perks({"luckyRecovery"}));
	exactLeadership();
	startBattle();
	auto * actor = stack("core:titan", BattleSide::ATTACKER, BattleHex(leftHex), 100);
	auto * shooter = stack("core:titan", BattleSide::DEFENDER, BattleHex(rightHex + 6), 10);
	beginCombat();
	const auto ordinary = damage(shooter, actor, true);
	ASSERT_EQ(ordinary, 400);
	ASSERT_TRUE(act(actor, BattleAction::makeMove(actor, BattleHex(leftHex - 2))));
	EXPECT_EQ(actor->getPosition(), BattleHex(leftHex - 2));
	EXPECT_EQ(dappledHexes(actor), 2);
	EXPECT_EQ(damage(shooter, actor, true), ordinary);
	// Movement accumulates within one activation: the third hex earns cover.
	apply(revision::dappledMovement(*battle(), *actor, true, 1));
	EXPECT_EQ(dappledHexes(actor), 3);
	EXPECT_EQ(damage(shooter, actor, true), 320); // floor(10 * 40 * 0.8).
	const auto melee = BattleAttackInfo(shooter, actor, 0, false);
	EXPECT_EQ(battle()->calculateDmgRange(melee).damage.min, ordinary);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(dappledHexes(actor), 0);
	EXPECT_EQ(damage(shooter, actor, true), ordinary);
}

TEST_F(SylvanTactics41Test, ThreeHexWalkProtectsWhileForcedOrTeleportedMovementDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(perks({"luckyRecovery"}));
	exactLeadership();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::TELEPORT);
	setTestSpellPointTotal(attackerSideHero, 100);
	startBattle();
	auto * actor = stack("core:titan", BattleSide::ATTACKER, BattleHex(leftHex), 100);
	auto * shooter = stack("core:titan", BattleSide::DEFENDER, BattleHex(rightHex + 6), 10);
	beginCombat();
	battle()->activeStack = actor->unitId();
	BattleActionProcessor automatic(gameHandler->battles.get(), gameHandler.get());
	ASSERT_TRUE(automatic.makeAutomaticBattleAction(*battle(), BattleAction::makeMove(actor, BattleHex(leftHex - 3))));
	EXPECT_EQ(actor->getPosition(), BattleHex(leftHex - 3));
	EXPECT_EQ(dappledHexes(actor), 0);
	battle()->activeStack = actor->unitId();
	BattleAction teleport;
	teleport.actionType = EActionType::HERO_SPELL;
	teleport.side = BattleSide::ATTACKER;
	teleport.spell = SpellID::TELEPORT;
	teleport.aimToUnit(actor);
	teleport.aimToHex(BattleHex(7, 3));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), teleport));
	EXPECT_EQ(actor->getPosition(), BattleHex(7, 3));
	EXPECT_EQ(dappledHexes(actor), 0);
	// Not adjacent to the shooter, so no ranged melee-range penalty.
	ASSERT_TRUE(act(actor, BattleAction::makeMove(actor, BattleHex(10, 3))));
	EXPECT_EQ(dappledHexes(actor), 3);
	EXPECT_EQ(damage(shooter, actor, true), 320);
	EXPECT_TRUE(revision::dappledMovement(*battle(), *actor, false, 5).add.empty());
}

TEST_F(SylvanTactics41Test, OrdinaryRetaliationGrantsInitiativeThatRefreshesWithoutStacking)
{
	ASSERT_NO_FATAL_FAILURE(perks({"serendipity", "natureSProvidence"}));
	startBattle();
	auto * centaur = stack("core:centaur", BattleSide::ATTACKER, BattleHex(leftHex), 50);
	// Two fast enemies: the one activated first attacks while the other still
	// precedes the retaliator, so its own next activation cannot expire the marker.
	auto * left = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 1);
	auto * right = stack("core:angel", BattleSide::DEFENDER, BattleHex(leftHex - 1), 1);
	beginCombat();
	const auto * first = battle()->battleActiveUnit() == right ? right : left;
	const auto * second = first == left ? right : left;
	ASSERT_EQ(battle()->battleActiveUnit(), first);
	const int initiative = centaur->getInitiative();
	ASSERT_TRUE(attack(first, centaur->getPosition()));
	// Morale or the queue may hand the turn to either angel, never to the retaliator.
	ASSERT_NE(battle()->battleActiveUnit(), centaur);
	EXPECT_EQ(redeployments(centaur), 1);
	EXPECT_EQ(centaur->getInitiative(), initiative + revision::QUICK_REDEPLOYMENT_INITIATIVE);
	BattleAttackInfo counter(centaur, second, 0, false);
	counter.retaliation = true;
	const auto refresh = revision::quickRedeployment(*battle(), counter);
	EXPECT_EQ(refresh.remove.size(), 1u);
	apply(refresh);
	EXPECT_EQ(redeployments(centaur), 1);
	EXPECT_EQ(centaur->getInitiative(), initiative + revision::QUICK_REDEPLOYMENT_INITIATIVE);
	EXPECT_EQ(revision::statusRows(centaur), std::vector<std::string>{
		"Quick Redeployment: +2 Initiative until this stack's next activation."});
	battle()->nextTurn(centaur->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(redeployments(centaur), 0);
	EXPECT_EQ(centaur->getInitiative(), initiative);
}

TEST_F(SylvanTactics41Test, AttackingAndNonOrdinaryRetaliationContextsGrantNoInitiative)
{
	ASSERT_NO_FATAL_FAILURE(perks({"serendipity", "natureSProvidence"}));
	startBattle();
	auto * centaur = stack("core:centaur", BattleSide::ATTACKER, BattleHex(leftHex), 20);
	auto * target = stack("core:pikeman", BattleSide::DEFENDER, BattleHex(rightHex), 50);
	beginCombat();
	ASSERT_TRUE(attack(centaur, target->getPosition()));
	EXPECT_EQ(redeployments(centaur), 0);
	BattleAttackInfo counter(centaur, target, 0, false);
	counter.retaliation = true;
	EXPECT_FALSE(revision::quickRedeployment(*battle(), counter).add.empty());
	for(int context = 0; context < 3; ++context)
	{
		auto variant = counter;
		variant.secondaryAttack = context == 0;
		variant.bracePreemptive = context == 1;
		variant.preemptiveDamagePercent = context == 2 ? 50 : 0;
		EXPECT_TRUE(revision::quickRedeployment(*battle(), variant).add.empty()) << context;
	}
	// An enemy creature's retaliation never reads the Sylvan hero's perk.
	BattleAttackInfo enemyCounter(target, centaur, 0, false);
	enemyCounter.retaliation = true;
	EXPECT_TRUE(revision::quickRedeployment(*battle(), enemyCounter).add.empty());
}

TEST_F(SylvanTactics41Test, HuntingPackHitsEveryRampartAttackAgainstTargetsThatHaveNotActed)
{
	ASSERT_NO_FATAL_FAILURE(perks({"elvenPrecision"}));
	exactLeadership();
	startBattle();
	auto * dwarf = stack("core:dwarf", BattleSide::ATTACKER, BattleHex(leftHex), 20);
	auto * angel = stack("core:angel", BattleSide::ATTACKER, BattleHex(leftHex - 3), 1);
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*battle(), dwarf, target), 15);
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*battle(), angel, target), 0);
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*battle(), target, dwarf), 0);
	const auto boosted = damage(dwarf, target);
	EXPECT_EQ(damage(dwarf, target, false, true), boosted);
	// Waiting is not an action: the target still counts as not having acted.
	target->afterWait();
	ASSERT_TRUE(target->waited());
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*battle(), dwarf, target), 15);
	target->completedActionThisRound = true;
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*battle(), dwarf, target), 0);
	const auto ordinary = damage(dwarf, target);
	// The damage script rounds once after all multipliers.
	EXPECT_GE(boosted, ordinary * 115 / 100);
	EXPECT_LE(boosted, ordinary * 115 / 100 + 1);
}

TEST_F(SylvanTactics41Policy3Test, CapturedPolicyThreeKeepsWaitingDefendAndCentaurMeanings)
{
	ASSERT_NO_FATAL_FAILURE(perks({"luckyRecovery", "natureSProvidence"}));
	EXPECT_FALSE(newHorizonsHeroes::usesSylvanTacticsV4(attackerSideHero->getPerkState().getRules()));
	SylvanLuckState fortune;
	newHorizonsSylvanTactics::configure(*attackerSideHero, fortune);
	EXPECT_TRUE(fortune.tactics.dappledCover);
	EXPECT_TRUE(fortune.tactics.quickRedeployment);
	startBattle();
	auto * centaur = stack("core:centaur", BattleSide::ATTACKER, BattleHex(leftHex), 20);
	auto * target = stack("core:pikeman", BattleSide::DEFENDER, BattleHex(rightHex), 1);
	EXPECT_TRUE(revision::dappledMovement(*battle(), *centaur, true, 5).add.empty());
	BattleAttackInfo counter(centaur, target, 0, false);
	counter.retaliation = true;
	EXPECT_TRUE(revision::quickRedeployment(*battle(), counter).add.empty());
	beginCombat();
	ASSERT_TRUE(attack(target, centaur->getPosition()));
	EXPECT_EQ(redeployments(centaur), 0);
}

TEST_F(SylvanTactics41Policy3Test, CapturedPolicyThreeHuntingPackStaysTheCentaurFlank)
{
	ASSERT_NO_FATAL_FAILURE(perks({"elvenPrecision"}));
	startBattle();
	auto * centaur = stack("core:centaur", BattleSide::ATTACKER, BattleHex(leftHex), 20);
	auto * target = stack("core:pikeman", BattleSide::DEFENDER, BattleHex(rightHex), 1);
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*battle(), centaur, target), 0);
	auto * dwarf = stack("core:dwarf", BattleSide::ATTACKER, BattleHex(rightHex - GameConstants::BFIELD_WIDTH), 20);
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*battle(), centaur, target), 25);
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*battle(), dwarf, target), 0);
}
}
