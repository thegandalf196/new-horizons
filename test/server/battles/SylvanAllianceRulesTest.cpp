/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/battle/NewHorizonsSylvanTacticsV2.h"
#include "../../../lib/battle/NewHorizonsSylvanTactics.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace sylvan = newHorizonsSylvanTacticsV2;
class SylvanAllianceRulesTest : public BattleTestFixture
{
protected:
	virtual int policy() const { return 2; }
	void SetUp() override { BattleTestFixture::SetUp(); startGame(); }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		rules["sylvanTacticsPolicy"].Integer() = policy();
		if(policy() == 2)
		{
			// Historical captures own their effect payloads. Do not use the
			// installed policy-3 parameters for the retired policy-2 effects.
			for(auto & row : rules["skills"]["new-horizons:sylvanLuck"]["perks"].Vector())
			{
				const auto & id = row["id"].String();
				const auto parameter = [&](const char * field, int value)
				{
					row["effect"]["parameters"] = JsonNode();
					row["effect"]["parameters"][field] = JsonNode(value);
				};
				if(id.ends_with(".forestSFavor")) parameter("maximumNegativeSpellRounds", 1);
				if(id.ends_with(".serendipity")) parameter("retaliationsConsumed", 1);
				if(id.ends_with(".luckyRecovery")) parameter("attackDamageReductionPercent", 20);
				if(id.ends_with(".natureSProvidence")) parameter("enemySpellCostBonus", 2);
			}
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
	}
	void perk(const char * suffix, int tier = 1)
	{
		if(tier == 2) perk("elvenPrecision", 1);
		if(tier == 3) perk("natureSProvidence", 2);
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
		attackerSideHero->setSecSkillLevel(skill, tier, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", std::string("new-horizons:sylvanLuck.") + suffix});
	}
	CStack * stack(const char * creature, BattleSide side, BattleHex hex, int count = 10)
	{
		return addStack(side, creatureByName(creature), hex, count);
	}
	Bonus timed(SpellID spell, int rounds = 5)
	{
		Bonus result(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 1, BonusSourceID(spell));
		result.turnsRemain = rounds;
		return result;
	}
	int64_t damage(const CStack * actor, const CStack * target, bool ranged = false, bool lucky = false)
	{
		BattleAttackInfo info(actor, target, 0, ranged);
		info.luckyStrike = lucky;
		return battle()->calculateDmgRange(info).damage.min;
	}
};

TEST_F(SylvanAllianceRulesTest, HuntingPackNeedsAnotherFriendlyRampartNeighborAndUsesNormalDamage)
{
	perk("elvenPrecision");
	startBattle();
	auto * actor = stack("core:centaur", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	EXPECT_EQ(sylvan::huntingPackBonus(*battle(), actor, target), 0);
	const auto ordinary = damage(actor, target);
	auto * ally = stack("core:dwarf", BattleSide::ATTACKER, BattleHex(rightHex - GameConstants::BFIELD_WIDTH));
	EXPECT_EQ(sylvan::huntingPackBonus(*battle(), actor, target), 25);
	EXPECT_EQ(damage(actor, target), ordinary * 125 / 100);
	ally->setPosition(BattleHex(leftHex - 4));
	EXPECT_EQ(sylvan::huntingPackBonus(*battle(), actor, target), 0);
}

TEST_F(SylvanAllianceRulesTest, DisarmingShotSpendsOnlyAvailableFiniteCountersOnDamagingRangedHits)
{
	perk("serendipity");
	startBattle();
	auto * actor = stack("core:woodElf", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex + 4), 100);
	EXPECT_EQ(sylvan::disarmingShotRetaliations(*battle(), actor, target, true, 1), 1);
	EXPECT_EQ(sylvan::disarmingShotRetaliations(*battle(), actor, target, true, 0), 0);
	EXPECT_EQ(sylvan::disarmingShotRetaliations(*battle(), actor, target, false, 1), 0);
	target->counterAttacks.use();
	EXPECT_EQ(sylvan::disarmingShotRetaliations(*battle(), actor, target, true, 1), 0);
	target->counterAttacks.reset();
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::UNLIMITED_RETALIATIONS, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(sylvan::disarmingShotRetaliations(*battle(), actor, target, true, 1), 0);
}

TEST_F(SylvanAllianceRulesTest, AcceptedShotPersistsSpentCounterInVictimPacket)
{
	perk("serendipity");
	startBattle();
	auto * actor = stack("core:woodElf", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex + 4), 100);
	beginCombat();
	battle()->activeStack = actor->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(actor->unitSide()),
		BattleAction::makeShotAttack(actor, target)));
	EXPECT_EQ(target->counterAttacks.available(), 0);
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_EQ(sylvan::disarmingShotRetaliations(*battle(), actor, target, true, 1), 0);
}

TEST_F(SylvanAllianceRulesTest, BriarGripReadsOnlyLiveDendroidBindAndDoesNotStack)
{
	perk("luckyRecovery");
	startBattle();
	auto * tree = stack("core:dendroidGuard", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	Bonus root(BonusDuration::ONE_BATTLE, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BIND)));
	root.parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(tree->unitId()));
	target->addNewBonus(std::make_shared<Bonus>(root));
	EXPECT_EQ(sylvan::briarGripReduction(*battle(), target), 20);
	target->addNewBonus(std::make_shared<Bonus>(root));
	EXPECT_EQ(sylvan::briarGripReduction(*battle(), target), 20);
	tree->setPosition(BattleHex(leftHex - 4));
	EXPECT_EQ(sylvan::briarGripReduction(*battle(), target), 0);
	target->removeBonuses(Selector::type()(BonusType::BIND_EFFECT));
	EXPECT_EQ(sylvan::briarGripReduction(*battle(), target), 0);
}

TEST_F(SylvanAllianceRulesTest, DwarfNegativeCapRetainsNativeEarlierBreakConditions)
{
	perk("forestSFavor");
	startBattle();
	auto * dwarf = stack("core:battleDwarf", BattleSide::ATTACKER, BattleHex(leftHex));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	spells::BattleCast cast(battle(), defenderSideHero, spells::Mode::HERO, SpellID(SpellID::BLIND).toSpell());
	const auto mechanics = SpellID(SpellID::BLIND).toSpell()->battleMechanics(&cast);
	auto bonus = timed(SpellID::BLIND);
	bonus.duration |= BonusDuration::UNTIL_BEING_ATTACKED | BonusDuration::UNTIL_ATTACK | BonusDuration::STACK_GETS_TURN;
	SetStackEffect packet;
	packet.toAdd.emplace_back(dwarf->unitId(), std::vector<Bonus>{bonus});
	sylvan::adjustAppliedBonuses(*mechanics, packet);
	const auto & adjusted = packet.toAdd.front().second.front();
	EXPECT_EQ(adjusted.turnsRemain, 1);
	EXPECT_TRUE(adjusted.duration & BonusDuration::UNTIL_BEING_ATTACKED);
	EXPECT_TRUE(adjusted.duration & BonusDuration::UNTIL_ATTACK);
	EXPECT_TRUE(adjusted.duration & BonusDuration::N_TURNS);
	EXPECT_TRUE(adjusted.duration & BonusDuration::STACK_GETS_TURN);
	EXPECT_EQ(bonus.val, adjusted.val);
}

TEST_F(SylvanAllianceRulesTest, EvergreenAddsTwoOnlyToOwnFriendlyRampartRoundBuffs)
{
	perk("sharedFortune", 2);
	startBattle();
	auto * elf = stack("core:woodElf", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * foreign = stack("core:angel", BattleSide::ATTACKER, BattleHex(leftHex - 1));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::BLESS).toSpell());
	const auto mechanics = SpellID(SpellID::BLESS).toSpell()->battleMechanics(&cast);
	SetStackEffect packet;
	packet.toAdd.emplace_back(elf->unitId(), std::vector<Bonus>{timed(SpellID::BLESS)});
	packet.toAdd.emplace_back(foreign->unitId(), std::vector<Bonus>{timed(SpellID::BLESS)});
	sylvan::adjustAppliedBonuses(*mechanics, packet);
	EXPECT_EQ(packet.toAdd[0].second.front().turnsRemain, 7);
	EXPECT_EQ(packet.toAdd[1].second.front().turnsRemain, 5);
	packet.toAdd[0].second.front().duration = BonusDuration::UNTIL_ATTACK;
	packet.toAdd[0].second.front().turnsRemain = 5;
	sylvan::adjustAppliedBonuses(*mechanics, packet);
	EXPECT_EQ(packet.toAdd[0].second.front().turnsRemain, 5);
}

TEST_F(SylvanAllianceRulesTest, SilverWingsAddsTwoToOneStrongestEnemyDamper)
{
	perk("natureSProvidence", 2);
	startBattle();
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex + 4));
	const auto spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const int ordinary = battle()->battleGetSpellCost(spell, defenderSideHero);
	auto * pegasus = stack("core:pegasus", BattleSide::ATTACKER, BattleHex(leftHex));
	ASSERT_EQ(sylvan::extraDamper(*battle(), pegasus), 2);
	const int one = battle()->battleGetSpellCost(spell, defenderSideHero);
	EXPECT_EQ(one, ordinary + pegasus->valOfBonuses(BonusType::CHANGES_SPELL_COST_FOR_ENEMY) + 2);
	stack("core:silverPegasus", BattleSide::ATTACKER, BattleHex(leftHex - 2));
	EXPECT_EQ(battle()->battleGetSpellCost(spell, defenderSideHero), one);
}

TEST_F(SylvanAllianceRulesTest, SacredGroveProtectsOnlyFriendlyPositiveTemporaryMagicFromEnemyDispel)
{
	perk("fortunateAim", 2);
	startBattle();
	auto * unicorn = stack("core:unicorn", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * ally = stack("core:dwarf", BattleSide::ATTACKER, BattleHex(rightHex));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex + 4));
	spells::BattleCast hostile(battle(), defenderSideHero, spells::Mode::HERO, SpellID(SpellID::DISPEL).toSpell());
	spells::BattleCast own(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::DISPEL).toSpell());
	const auto enemyDispel = SpellID(SpellID::DISPEL).toSpell()->battleMechanics(&hostile);
	const auto ownDispel = SpellID(SpellID::DISPEL).toSpell()->battleMechanics(&own);
	const auto buff = timed(SpellID::BLESS);
	EXPECT_TRUE(sylvan::preservesBuffFromDispel(*enemyDispel, ally, buff));
	EXPECT_FALSE(sylvan::preservesBuffFromDispel(*ownDispel, ally, buff));
	EXPECT_FALSE(sylvan::preservesBuffFromDispel(*enemyDispel, ally, timed(SpellID::SLOW)));
	unicorn->setPosition(BattleHex(leftHex - 4));
	EXPECT_FALSE(sylvan::preservesBuffFromDispel(*enemyDispel, ally, buff));
}

TEST_F(SylvanAllianceRulesTest, KindredFlameFiltersFriendlyBreathVictimsWithoutChangingGeometry)
{
	perk("wildChance", 2);
	startBattle();
	auto * dragon = stack("core:greenDragon", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	auto * ally = stack("core:dwarf", BattleSide::ATTACKER, BattleHex(rightHex + 1));
	const auto geometry = battle()->getPotentiallyAttackableHexes(dragon, target, target->getPosition(), BattleHex::INVALID, BattleHex::INVALID);
	ASSERT_TRUE(geometry.friendlyCreaturePositions.contains(ally->getPosition()));
	EXPECT_TRUE(sylvan::spareFriendlyBreath(*battle(), dragon, ally));
	EXPECT_FALSE(sylvan::spareFriendlyBreath(*battle(), dragon, target));
	const auto victims = battle()->getAttackedCreatures(dragon, target->getPosition(), false);
	EXPECT_EQ(std::ranges::find(victims.first, ally), victims.first.end());
}

TEST_F(SylvanAllianceRulesTest, AncientAccordBypassesOnlyNativeFriendlyLevelImmunity)
{
	perk("cascadingFortune", 3);
	startBattle();
	auto * dragon = stack("core:greenDragon", BattleSide::ATTACKER, BattleHex(leftHex));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex + 4));
	spells::BattleCast blessing(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::BLESS).toSpell());
	spells::BattleCast curse(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::CURSE).toSpell());
	const auto positive = SpellID(SpellID::BLESS).toSpell()->battleMechanics(&blessing);
	const auto negative = SpellID(SpellID::CURSE).toSpell()->battleMechanics(&curse);
	EXPECT_TRUE(sylvan::levelImmunityAllows(*positive, dragon, false));
	EXPECT_FALSE(sylvan::levelImmunityAllows(*negative, dragon, false));
	auto artifact = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LEVEL_SPELL_IMMUNITY, BonusSource::ARTIFACT, 5, BonusSourceID(ArtifactID(0)));
	dragon->addNewBonus(artifact);
	EXPECT_FALSE(sylvan::ignoresNativeLevelImmunity(*positive, dragon, *artifact));
	EXPECT_FALSE(sylvan::levelImmunityAllows(*positive, dragon, false));
}

TEST_F(SylvanAllianceRulesTest, V2KeepsPerfectMomentAndDoesNotGenerateRetiredHistoriesAcrossRoundtrip)
{
	const int luck = attackerSideHero->valOfBonuses(BonusType::LUCK);
	perk("perfectMoment", 3);
	startBattle();
	// Eight Centaurs give integral ordinary damage after this fixture's defenses,
	// so the 350% assertion does not multiply an already rounded-down reference.
	auto * actor = stack("core:centaur", BattleSide::ATTACKER, BattleHex(leftHex), 8);
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	const auto ordinary = damage(actor, target);
	ASSERT_EQ(ordinary, 10);
	EXPECT_EQ(damage(actor, target, false, true), ordinary * 350 / 100);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::LUCK), luck);
	auto fortune = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	fortune.beginActivation(actor->unitId(), true, true, true);
	fortune.tactics.finishStrike(actor->unitId(), {target->unitId()}, {}, {}, true);
	EXPECT_TRUE(fortune.tactics.activated.empty());
	EXPECT_TRUE(fortune.tactics.shootersByTarget.empty());
	EXPECT_TRUE(fortune.tactics.covered.empty());
	EXPECT_FALSE(fortune.tactics.huntTriggered);
	EXPECT_FALSE(fortune.canUsePerfectMoment());
	CMemorySerializer storage;
	storage.oser & fortune;
	SylvanLuckState restored;
	storage.iser & restored;
	EXPECT_EQ(fortune, restored);
}

class SylvanHybridRulesTest : public SylvanAllianceRulesTest
{
protected:
	int policy() const override { return 3; }
	SylvanTacticsState configured() const
	{
		SylvanLuckState result;
		newHorizonsSylvanTactics::configure(*attackerSideHero, result);
		return result.tactics;
	}
	void expectNoRetiredHistory(const SylvanTacticsState & state)
	{
		EXPECT_FALSE(state.measuredAdvance);
		EXPECT_FALSE(state.coveringFire);
		EXPECT_FALSE(state.crossfire);
		EXPECT_FALSE(state.wildHunt);
		EXPECT_TRUE(state.covered.empty());
		EXPECT_TRUE(state.shootersByTarget.empty());
		EXPECT_FALSE(state.huntTriggered);
	}
};

class SylvanHybridBasicTest : public SylvanHybridRulesTest, public ::testing::WithParamInterface<int> {};
TEST_P(SylvanHybridBasicTest, EachBasicSlotEnablesExactlyItsSelectedMechanic)
{
	const char * ids[] = {"elvenPrecision", "forestSFavor", "serendipity", "luckyRecovery"};
	const int chosen = GetParam();
	perk(ids[chosen]);
	const auto state = configured();
	EXPECT_TRUE(state.enabled);
	EXPECT_EQ(state.fleetfoot, chosen == 1);
	EXPECT_EQ(state.patientHunter, chosen == 2);
	EXPECT_EQ(state.dappledCover, chosen == 3);
	EXPECT_FALSE(state.disruptiveStrike);
	EXPECT_FALSE(state.quickRedeployment);
	EXPECT_EQ(sylvan::hasPerk(attackerSideHero, ids[chosen]), chosen == 0);
	expectNoRetiredHistory(state);
}
INSTANTIATE_TEST_SUITE_P(Policy3, SylvanHybridBasicTest, ::testing::Range(0, 4));

class SylvanHybridAdvancedTest : public SylvanHybridRulesTest, public ::testing::WithParamInterface<int> {};
TEST_P(SylvanHybridAdvancedTest, EachAdvancedSlotEnablesOnlySelectedActivationHistory)
{
	const char * ids[] = {"sharedFortune", "natureSProvidence", "fortunateAim", "wildChance"};
	const int chosen = GetParam();
	perk(ids[chosen], 2);
	const auto state = configured();
	EXPECT_EQ(state.quickRedeployment, chosen == 1);
	EXPECT_EQ(state.disruptiveStrike, chosen == 2);
	EXPECT_FALSE(state.fleetfoot);
	EXPECT_FALSE(state.patientHunter);
	EXPECT_FALSE(state.dappledCover);
	EXPECT_EQ(sylvan::hasPerk(attackerSideHero, ids[chosen]), chosen == 0 || chosen == 3);
	expectNoRetiredHistory(state);
}
INSTANTIATE_TEST_SUITE_P(Policy3, SylvanHybridAdvancedTest, ::testing::Range(0, 4));

TEST_F(SylvanHybridRulesTest, FleetfootAppliesOnlyDuringTheFirstRealActivationIncludingWait)
{
	perk("forestSFavor");
	startBattle();
	auto * actor = stack("core:angel", BattleSide::ATTACKER, BattleHex(leftHex));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	auto & state = battle()->getSide(BattleSide::ATTACKER).sylvanLuck.tactics;
	const auto firstSpeed = actor->getMovementRange();
	state.beginActivation(actor->unitId(), true);
	EXPECT_EQ(actor->getMovementRange(), firstSpeed);
	actor->afterWait();
	state.endActivation();
	EXPECT_EQ(actor->getMovementRange(), firstSpeed - 1);
	state.beginActivation(actor->unitId(), true);
	EXPECT_EQ(actor->getMovementRange(), firstSpeed - 1);
	state.nextRound();
	EXPECT_EQ(actor->getMovementRange(), firstSpeed - 1);
	expectNoRetiredHistory(state);
}

TEST_F(SylvanHybridRulesTest, PatientHunterUsesTheSharedSingleAttackAfterWaitAllowance)
{
	perk("serendipity");
	// Isolate the perk's multiplier from fractional square-root Leadership.
	// Damage is rounded once after both factors, not before the perk applies.
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LEADERSHIP, BonusSource::OTHER, 100000, BonusSourceID()));
	startBattle();
	auto * actor = stack("core:angel", BattleSide::ATTACKER, BattleHex(leftHex), 10);
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	ASSERT_DOUBLE_EQ(battle()->battleLeadershipReadout(actor).efficiency, 1);
	const auto ordinary = damage(actor, target);
	ASSERT_EQ(ordinary, 500); // Ten 50-damage Angels against the same creature.
	actor->afterWait();
	EXPECT_EQ(damage(actor, target), 575); // floor(10 * 50 * 1.15).
	actor->afterAttack(false, false, true);
	EXPECT_EQ(damage(actor, target), ordinary);
	EXPECT_EQ(sylvan::disarmingShotRetaliations(*battle(), actor, target, true, 1), 0);
}

TEST_F(SylvanHybridRulesTest, DappledCoverExpiresAtTheDelayedActivationAndDoesNotEnableBriarGrip)
{
	perk("luckyRecovery");
	defenderSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LEADERSHIP, BonusSource::OTHER, 100000, BonusSourceID()));
	startBattle();
	auto * actor = stack("core:titan", BattleSide::ATTACKER, BattleHex(leftHex), 100);
	auto * shooter = stack("core:titan", BattleSide::DEFENDER, BattleHex(rightHex + 4), 10);
	ASSERT_DOUBLE_EQ(battle()->battleLeadershipReadout(shooter).efficiency, 1);
	const auto ordinary = damage(shooter, actor, true);
	ASSERT_EQ(ordinary, 400); // Ten 40-minimum-damage Titans against matching Defense.
	actor->afterWait();
	EXPECT_EQ(damage(shooter, actor, true), 320); // floor(10 * 40 * 0.8).
	auto & state = battle()->getSide(BattleSide::ATTACKER).sylvanLuck.tactics;
	state.beginActivation(actor->unitId(), true, false, true);
	EXPECT_EQ(damage(shooter, actor, true), ordinary);
	auto * tree = stack("core:dendroidGuard", BattleSide::ATTACKER, BattleHex(rightHex + 3));
	auto binding = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::BIND_EFFECT,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BIND)));
	binding->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(tree->unitId()));
	shooter->addNewBonus(binding);
	EXPECT_EQ(sylvan::briarGripReduction(*battle(), shooter), 0);
}

TEST_F(SylvanHybridRulesTest, HuntingPackSupportsNormalAndRetaliationDamageWithoutSelfSupport)
{
	perk("elvenPrecision");
	startBattle();
	auto * actor = stack("core:centaurCaptain", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	EXPECT_EQ(sylvan::huntingPackBonus(*battle(), actor, target), 0);
	stack("core:dwarf", BattleSide::ATTACKER, BattleHex(rightHex - GameConstants::BFIELD_WIDTH));
	EXPECT_EQ(sylvan::huntingPackBonus(*battle(), actor, target), 25);
	BattleAttackInfo normal(actor, target, 0, false);
	BattleAttackInfo retaliation = normal;
	retaliation.retaliation = true;
	EXPECT_EQ(battle()->calculateDmgRange(normal).damage.min, battle()->calculateDmgRange(retaliation).damage.min);
}

TEST_F(SylvanHybridRulesTest, DisruptiveStrikeStoresOnlyPositivePrimaryMeleeHitsUntilTargetActivation)
{
	perk("fortunateAim", 2);
	startBattle();
	auto * actor = stack("core:angel", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	auto & state = battle()->getSide(BattleSide::ATTACKER).sylvanLuck.tactics;
	const int initiative = target->getInitiative();
	const auto snapshot = newHorizonsSylvanTactics::captureStrike(*battle(), actor, target, {target}, false, true);
	newHorizonsSylvanTactics::finishStrike(snapshot, {{target->unitId(), 0}}, {}, state);
	EXPECT_EQ(target->getInitiative(), initiative);
	newHorizonsSylvanTactics::finishStrike(snapshot, {{target->unitId(), 1}}, {}, state);
	EXPECT_EQ(battle()->battleSylvanTacticsInitiativePercent(target), -15);
	newHorizonsSylvanTactics::finishStrike(snapshot, {{target->unitId(), 1}}, {}, state);
	EXPECT_EQ(state.disrupted.size(), 1);
	state.nextRound();
	EXPECT_LT(target->getInitiative(), initiative);
	state.beginActivation(target->unitId(), false);
	EXPECT_EQ(target->getInitiative(), initiative);
	expectNoRetiredHistory(state);
}

TEST_F(SylvanHybridRulesTest, AcceptedMeleeAndNormalRetaliationBothApplyDisruptiveStrike)
{
	perk("fortunateAim", 2);
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
	defenderSideHero->setSecSkillLevel(skill, 1, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"});
	defenderSideHero->setSecSkillLevel(skill, 2, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.fortunateAim"});
	startBattle();
	auto * actor = stack("core:angel", BattleSide::ATTACKER, BattleHex(leftHex), 100);
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	beginCombat();
	battle()->activeStack = actor->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(actor->unitSide()),
		BattleAction::makeMeleeAttack(actor, target, actor->getPosition())));
	ASSERT_GE(server.attacks.size(), 2);
	EXPECT_FALSE(server.attacks[0].counter());
	EXPECT_TRUE(server.attacks[1].counter());
	ASSERT_TRUE(server.attacks[0].fortuneState);
	ASSERT_TRUE(server.attacks[1].fortuneState);
	EXPECT_TRUE(server.attacks[0].fortuneState->tactics.disrupted.contains(target->unitId()));
	EXPECT_TRUE(server.attacks[1].fortuneState->tactics.disrupted.contains(actor->unitId()));
}

TEST_F(SylvanHybridRulesTest, ExplicitSpellLikeDamageCannotApplyOrForgeDisruptiveStrike)
{
	perk("fortunateAim", 2);
	startBattle();
	auto * actor = stack("core:angel", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	BattleAttack packet;
	packet.stackAttacking = actor->unitId();
	packet.flags = BattleAttack::SPELL_LIKE;
	packet.fortuneSide = BattleSide::ATTACKER;
	packet.fortuneState = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	BattleStackAttacked hit;
	hit.stackAttacked = target->unitId();
	hit.attackerID = actor->unitId();
	hit.damageAmount = 10;
	hit.flags = BattleStackAttacked::SPELL_EFFECT;
	packet.bsa.push_back(hit);
	EXPECT_NO_THROW(newHorizonsSylvanTactics::validateStrikeTransition(*battle(), packet));
	packet.fortuneState->tactics.disrupted.insert(target->unitId());
	EXPECT_THROW(newHorizonsSylvanTactics::validateStrikeTransition(*battle(), packet), std::runtime_error);
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).tactics.disrupted.empty());
	EXPECT_FALSE(newHorizonsSylvanTactics::captureStrike(*battle(), actor, target, {target}, false, false).valid);
}

TEST_F(SylvanHybridRulesTest, QuickRedeploymentSnapshotsDefendWithoutSilverWingsAndRoundtripsSpentState)
{
	perk("natureSProvidence", 2);
	startBattle();
	auto * actor = stack("core:pegasus", BattleSide::ATTACKER, BattleHex(leftHex));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	const auto ordinary = actor->getMovementRange();
	EXPECT_EQ(sylvan::extraDamper(*battle(), actor), 0);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION,
		BonusType::UNIT_DEFENDING, BonusSource::OTHER, 1, BonusSourceID()));
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_EQ(actor->getMovementRange(), ordinary);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(actor->getMovementRange(), ordinary + 2);
	CMemorySerializer storage;
	storage.oser & fortune;
	SylvanLuckState restored;
	storage.iser & restored;
	EXPECT_EQ(fortune, restored);
	fortune.tactics.endActivation();
	EXPECT_EQ(actor->getMovementRange(), ordinary);
	expectNoRetiredHistory(fortune.tactics);
}

TEST_F(SylvanHybridRulesTest, EvergreenRetainsOwnHeroRoundBuffScopeWithoutDwarvenHardiness)
{
	perk("sharedFortune", 2);
	startBattle();
	auto * dwarf = stack("core:battleDwarf", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * foreign = stack("core:angel", BattleSide::ATTACKER, BattleHex(leftHex - 1));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	spells::BattleCast own(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::BLESS).toSpell());
	const auto positive = SpellID(SpellID::BLESS).toSpell()->battleMechanics(&own);
	EXPECT_EQ(sylvan::adjustedTimedRounds(*positive, dwarf, 5), 7);
	EXPECT_EQ(sylvan::adjustedTimedRounds(*positive, foreign, 5), 5);
	EXPECT_EQ(sylvan::adjustedTimedRounds(*positive, dwarf, 0), 0);
	spells::BattleCast creature(battle(), dwarf, spells::Mode::CREATURE_ACTIVE, SpellID(SpellID::BLESS).toSpell());
	const auto creatureBuff = SpellID(SpellID::BLESS).toSpell()->battleMechanics(&creature);
	EXPECT_EQ(sylvan::adjustedTimedRounds(*creatureBuff, dwarf, 5), 5);
	spells::BattleCast hostile(battle(), defenderSideHero, spells::Mode::HERO, SpellID(SpellID::BLIND).toSpell());
	const auto negative = SpellID(SpellID::BLIND).toSpell()->battleMechanics(&hostile);
	EXPECT_EQ(sylvan::adjustedTimedRounds(*negative, dwarf, 5), 5);
}

TEST_F(SylvanHybridRulesTest, FleetfootDoesNotAlsoShortenHostileDwarfSpells)
{
	perk("forestSFavor");
	startBattle();
	auto * dwarf = stack("core:battleDwarf", BattleSide::ATTACKER, BattleHex(leftHex));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	spells::BattleCast hostile(battle(), defenderSideHero, spells::Mode::HERO, SpellID(SpellID::BLIND).toSpell());
	const auto negative = SpellID(SpellID::BLIND).toSpell()->battleMechanics(&hostile);
	EXPECT_EQ(sylvan::adjustedTimedRounds(*negative, dwarf, 5), 5);
	SetStackEffect packet;
	packet.toAdd.emplace_back(dwarf->unitId(), std::vector<Bonus>{timed(SpellID::BLIND)});
	sylvan::adjustAppliedBonuses(*negative, packet);
	EXPECT_EQ(packet.toAdd.front().second.front().turnsRemain, 5);
}

TEST_F(SylvanHybridRulesTest, KindredFlameRetainsFriendlyBreathFiltering)
{
	perk("wildChance", 2);
	startBattle();
	auto * dragon = stack("core:goldDragon", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex));
	auto * ally = stack("core:dwarf", BattleSide::ATTACKER, BattleHex(rightHex + 1));
	EXPECT_TRUE(sylvan::spareFriendlyBreath(*battle(), dragon, ally));
	EXPECT_FALSE(sylvan::spareFriendlyBreath(*battle(), dragon, target));
	const auto victims = battle()->getAttackedCreatures(dragon, target->getPosition(), false);
	EXPECT_EQ(std::ranges::find(victims.first, ally), victims.first.end());
	expectNoRetiredHistory(configured());
}

TEST_F(SylvanHybridRulesTest, DisruptiveStrikeDoesNotAlsoActivateRetiredSacredGrove)
{
	perk("fortunateAim", 2);
	startBattle();
	stack("core:unicorn", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * ally = stack("core:dwarf", BattleSide::ATTACKER, BattleHex(rightHex));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex + 4));
	spells::BattleCast hostile(battle(), defenderSideHero, spells::Mode::HERO, SpellID(SpellID::DISPEL).toSpell());
	const auto enemyDispel = SpellID(SpellID::DISPEL).toSpell()->battleMechanics(&hostile);
	EXPECT_FALSE(sylvan::preservesBuffFromDispel(*enemyDispel, ally, timed(SpellID::BLESS)));
}

TEST_F(SylvanHybridRulesTest, AncientAccordRetainsHeroOnlyFriendlyNativeImmunityException)
{
	perk("cascadingFortune", 3);
	startBattle();
	auto * dragon = stack("core:goldDragon", BattleSide::ATTACKER, BattleHex(leftHex));
	stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex + 4));
	spells::BattleCast blessing(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::BLESS).toSpell());
	const auto positive = SpellID(SpellID::BLESS).toSpell()->battleMechanics(&blessing);
	EXPECT_TRUE(sylvan::levelImmunityAllows(*positive, dragon, false));
	spells::BattleCast creature(battle(), dragon, spells::Mode::CREATURE_ACTIVE, SpellID(SpellID::BLESS).toSpell());
	const auto creatureBuff = SpellID(SpellID::BLESS).toSpell()->battleMechanics(&creature);
	EXPECT_FALSE(sylvan::levelImmunityAllows(*creatureBuff, dragon, false));
	auto artifact = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LEVEL_SPELL_IMMUNITY,
		BonusSource::ARTIFACT, 5, BonusSourceID(ArtifactID(0)));
	dragon->addNewBonus(artifact);
	EXPECT_FALSE(sylvan::levelImmunityAllows(*positive, dragon, false));
}

TEST_F(SylvanHybridRulesTest, PerfectMomentKeepsThreePointFiveDamageWithoutGrantingLuckOrWildHunt)
{
	const int luck = attackerSideHero->valOfBonuses(BonusType::LUCK);
	perk("perfectMoment", 3);
	startBattle();
	auto * actor = stack("core:centaur", BattleSide::ATTACKER, BattleHex(leftHex), 8);
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	const auto ordinary = damage(actor, target);
	ASSERT_EQ(ordinary, 10);
	EXPECT_EQ(damage(actor, target, false, true), ordinary * 350 / 100);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::LUCK), luck);
	EXPECT_TRUE(configured().perfectMoment);
	expectNoRetiredHistory(configured());
}

TEST_F(SylvanHybridRulesTest, CapturedPolicyAndPerkSurviveRoundtripWithoutInstallingAnotherRoster)
{
	perk("forestSFavor");
	CMemorySerializer storage;
	storage.oser & attackerSideHero->getPerkState();
	newHorizonsHeroes::PerkState restored;
	storage.iser & restored;
	EXPECT_EQ(restored.rules, attackerSideHero->getPerkState().rules);
	EXPECT_TRUE(newHorizonsHeroes::usesSylvanTacticsV3(restored.rules));
	EXPECT_FALSE(newHorizonsHeroes::usesSylvanTacticsV2(restored.rules));
	EXPECT_TRUE(restored.hasPerkForTier("new-horizons:sylvanLuck", 1));
	startBattle();
	auto fortune = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	fortune.tactics.beginActivation(42, true);
	fortune.tactics.endActivation();
	CMemorySerializer stateStorage;
	stateStorage.oser & fortune;
	SylvanLuckState loaded;
	stateStorage.iser & loaded;
	EXPECT_EQ(loaded, fortune);
	EXPECT_EQ(loaded.tactics.speedBonus(42), 0);
	expectNoRetiredHistory(loaded.tactics);
}
