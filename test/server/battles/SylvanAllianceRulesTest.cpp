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
	void SetUp() override { BattleTestFixture::SetUp(); startGame(); }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		rules["sylvanTacticsPolicy"].Integer() = 2;
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
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::UNLIMITED_RETALIATIONS, BonusSource::OTHER, 1));
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
	auto artifact = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LEVEL_SPELL_IMMUNITY, BonusSource::ARTIFACT, 5);
	dragon->addNewBonus(artifact);
	EXPECT_FALSE(sylvan::ignoresNativeLevelImmunity(*positive, dragon, *artifact));
	EXPECT_FALSE(sylvan::levelImmunityAllows(*positive, dragon, false));
}

TEST_F(SylvanAllianceRulesTest, V2KeepsPerfectMomentAndDoesNotGenerateRetiredHistoriesAcrossRoundtrip)
{
	const int luck = attackerSideHero->valOfBonuses(BonusType::LUCK);
	perk("perfectMoment", 3);
	startBattle();
	auto * actor = stack("core:centaur", BattleSide::ATTACKER, BattleHex(leftHex));
	auto * target = stack("core:angel", BattleSide::DEFENDER, BattleHex(rightHex), 100);
	const auto ordinary = damage(actor, target);
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
