/*
 * BattleShroudStrikeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/BattleShroudStrike.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include <bit>

namespace
{
// Independent pre-extraction expressions: deliberately three real flanking queries.
BattleShroudStrike legacyThreeQueries(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, unsigned & calls)
{
	const auto qualifyingSide = [&]() -> std::optional<BattleSide>
	{
		++calls;
		if(!battle.battleIsShroudFlankingAttack(attack))
			return {};
		const auto side = battle.playerToSide(battle.battleGetOwner(attack.attacker));
		if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
			return {};
		return side;
	};
	BattleShroudStrike result;
	if(const auto side = qualifyingSide())
		result.evasiveShroud = newHorizonsShroud::hasEvasiveShroud(battle.battleGetFightingHero(*side));
	if(const auto side = qualifyingSide())
		result.ambusher = newHorizonsShroud::ambusherDamagePercent(
			battle.battleGetFightingHero(*side), attack.attacker) > 0;
	if(const auto side = qualifyingSide())
		if(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
			battle.battleGetFightingHero(*side), attack.defender, *side) > 0)
			result.shadowAssaultSide = *side;
	return result;
}

class BattleShroudStrikeTest : public ProjectedTargetHitFixture
{
protected:
	using Snapshot = std::map<uint32_t, std::pair<JsonNode, std::vector<JsonNode>>>;
	void selectShroud(bool shadow = false)
	{
		const std::string skill(newHorizonsShroud::SKILL_ID);
		const SecondarySkill id(SecondarySkill::decode(skill));
		ASSERT_GE(id.getNum(), 0);
		for(const auto & [rank, perk] : std::vector<std::pair<MasteryLevel::Type, std::string_view>>{
			{MasteryLevel::BASIC, shadow ? newHorizonsShroud::SHADOW_ASSAULT_PERK_ID : newHorizonsShroud::AMBUSHER_PERK_ID},
			{MasteryLevel::ADVANCED, newHorizonsShroud::EVASIVE_SHROUD_PERK_ID}})
		{
			defenderSideHero->setSecSkillLevel(id, rank, ChangeValueMode::ABSOLUTE);
			ASSERT_TRUE(defenderSideHero->getPerkState().canAcquirePerk(skill, std::string(perk),
				defenderSideHero->getPerkSkillRank(skill)));
			defenderSideHero->applyPerkSelection({skill, std::string(perk)});
			ASSERT_TRUE(defenderSideHero->hasActivePerk(skill, std::string(perk)));
		}
		// Positive facts concern this actor's OWN hero, not hidden enemy policy.
		ASSERT_EQ(child->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
		live = std::make_shared<CPlayerBattleCallback>(battle(), defenderSideHero->getOwner());
		ASSERT_EQ(live->battleGetMySide(), BattleSide::DEFENDER);
		resetChild();
		ASSERT_EQ(child->battleGetFightingHero(BattleSide::DEFENDER), defenderSideHero);
		ASSERT_EQ(child->battleGetFightingHero(BattleSide::ATTACKER), nullptr);
	}
	Snapshot snapshot(const std::shared_ptr<HypotheticBattle> & branch)
	{
		Snapshot result;
		for(const auto * unit : branch->battleGetAllUnits(false))
		{
			const auto value = branch->getForUpdate(unit->unitId());
			std::vector<JsonNode> bonuses;
			const auto owners = value->getUnstackedBonuses(Selector::all);
			for(const auto & bonus : *owners)
				bonuses.push_back(bonus->toJsonNode());
			result.emplace(unit->unitId(), std::pair{value->save(), std::move(bonuses)});
		}
		return result;
	}
	auto rng()
	{
		return std::pair(projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())));
	}
	BattleAttackInfo rear(const std::shared_ptr<HypotheticBattle> & branch)
	{
		const auto actor = branch->getForUpdate(incoming->unitId());
		const auto target = branch->getForUpdate(braced->unitId());
		for(const auto & position : target->getSurroundingHexes())
		{
			if(!position.isAvailable() || branch->battleGetUnitByPos(position))
				continue;
			actor->setPosition(position);
			BattleAttackInfo attack(actor.get(), target.get(), 0, false);
			if(branch->battleIsShroudFlankingAttack(attack))
				return attack;
		}
		throw std::runtime_error("Fixture needs one unoccupied rear-facing adjacent hex");
	}
	BattleShroudStrike expectOracle(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
	{
		const auto random = rng();
		unsigned calls = 0;
		const auto expected = legacyThreeQueries(battle, attack, calls);
		EXPECT_EQ(calls, 3u);
		const auto actual = evaluateBattleShroudStrike(battle, attack);
		EXPECT_EQ(actual, expected);
		EXPECT_EQ(rng(), random);
		return actual;
	}
};

TEST_F(BattleShroudStrikeTest, NoPerkAndExcludedModesMatchIndependentThreeQueryOracle)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = rear(child);
	const auto before = snapshot(child);
	EXPECT_EQ(expectOracle(*child, attack), BattleShroudStrike{});
	for(int mode = 0; mode < 3; ++mode)
	{
		auto excluded = attack;
		excluded.shooting = mode == 0;
		excluded.physicalDamage = mode != 1;
		excluded.secondaryAttack = mode == 2;
		EXPECT_EQ(expectOracle(*child, excluded), BattleShroudStrike{});
	}
	EXPECT_EQ(snapshot(child), before);
}

TEST_F(BattleShroudStrikeTest, SpentMarkersPositionAndSiblingBranchAreRequeried)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectShroud());
	const auto attack = rear(child);
	const auto initial = expectOracle(*child, attack);
	ASSERT_TRUE(initial.ambusher);
	ASSERT_TRUE(initial.evasiveShroud);
	const auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto siblingAttack = rear(sibling);
	const auto siblingBefore = snapshot(sibling);
	const auto actor = child->getForUpdate(incoming->unitId());
	actor->addUnitBonus({newHorizonsShroud::ambusherSpentMarker()});
	EXPECT_FALSE(expectOracle(*child, attack).ambusher);
	EXPECT_TRUE(expectOracle(*child, attack).evasiveShroud);
	EXPECT_TRUE(expectOracle(*sibling, siblingAttack).ambusher);
	actor->removeUnitBonus(CSelector(newHorizonsShroud::isAmbusherSpentMarker));
	EXPECT_EQ(expectOracle(*child, attack), initial);
	actor->setPosition(BattleHex(1));
	EXPECT_EQ(expectOracle(*child, BattleAttackInfo(actor.get(), attack.defender, 0, false)), BattleShroudStrike{});
	EXPECT_EQ(snapshot(sibling), siblingBefore);
}

TEST_F(BattleShroudStrikeTest, ShadowAssaultTargetSideAndHostileControlStayLive)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectShroud(true));
	const auto attack = rear(child);
	ASSERT_EQ(expectOracle(*child, attack).shadowAssaultSide, BattleSide::DEFENDER);
	const auto target = child->getForUpdate(braced->unitId());
	target->addUnitBonus({newHorizonsShroud::shadowAssaultSpentMarker(BattleSide::DEFENDER)});
	EXPECT_FALSE(expectOracle(*child, attack).shadowAssaultSide);
	target->removeUnitBonus(CSelector([](const Bonus * bonus)
		{ return newHorizonsShroud::isShadowAssaultSpentMarker(bonus, BattleSide::DEFENDER); }));
	EXPECT_EQ(expectOracle(*child, attack).shadowAssaultSide, BattleSide::DEFENDER);
	const auto actor = child->getForUpdate(incoming->unitId());
	actor->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_EQ(child->battleGetOwner(actor.get()), child->battleGetOwner(target.get()));
	EXPECT_EQ(expectOracle(*child, attack), BattleShroudStrike{});
}

TEST_F(BattleShroudStrikeTest, FreshRetaliationAndCleaveQueriesDoNotReuseThePrimaryFacts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectShroud());
	const auto attack = rear(child);
	ASSERT_TRUE(expectOracle(*child, attack).ambusher);
	auto retaliation = BattleAttackInfo(attack.defender, attack.attacker, 0, false);
	retaliation.retaliation = true;
	EXPECT_EQ(expectOracle(*child, retaliation), BattleShroudStrike{});
	const auto actor = child->getForUpdate(incoming->unitId());
	actor->addUnitBonus({newHorizonsShroud::ambusherSpentMarker()});
	auto cleave = attack;
	cleave.cleaveDamagePercent = 50;
	EXPECT_FALSE(expectOracle(*child, cleave).ambusher);
	EXPECT_TRUE(expectOracle(*child, cleave).evasiveShroud);
}

TEST_F(BattleShroudStrikeTest, RemovedQueryWarmupsKeepAppliedScoreBitsStateAndBothRngs)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectShroud());
	const auto expected = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto actual = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto legacyAttack = rear(expected);
	const auto sharedAttack = rear(actual);
	const auto original = snapshot(child);
	const auto random = rng();
	unsigned calls = 0;
	EXPECT_EQ(legacyThreeQueries(*expected, legacyAttack, calls), evaluateBattleShroudStrike(*actual, sharedAttack));
	EXPECT_EQ(calls, 3u);
	DamageCache expectedCache, actualCache;
	expectedCache.buildDamageCache(expected, BattleSide::DEFENDER);
	actualCache.buildDamageCache(actual, BattleSide::DEFENDER);
	BattleExchangeVariant reference, result;
	const auto run = [&](BattleExchangeVariant & value, DamageCache & cache,
		const std::shared_ptr<HypotheticBattle> & branch)
	{
		return value.trackAttack(branch->getForUpdate(incoming->unitId()), branch->getForUpdate(braced->unitId()),
			false, true, cache, branch, false, true);
	};
	EXPECT_EQ(std::bit_cast<uint32_t>(run(reference, expectedCache, expected)),
		std::bit_cast<uint32_t>(run(result, actualCache, actual)));
	EXPECT_EQ(std::bit_cast<uint32_t>(reference.getScore().ourDamageReduce),
		std::bit_cast<uint32_t>(result.getScore().ourDamageReduce));
	EXPECT_EQ(std::bit_cast<uint32_t>(reference.getScore().enemyDamageReduce),
		std::bit_cast<uint32_t>(result.getScore().enemyDamageReduce));
	EXPECT_EQ(snapshot(expected), snapshot(actual));
	EXPECT_EQ(snapshot(child), original);
	EXPECT_EQ(rng(), random);
}

TEST_F(BattleShroudStrikeTest, FormationNeighborChangeInvalidatesTheNextStrikeFacts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectShroud());
	const auto originalAttack = rear(child);
	const auto rearPosition = originalAttack.attacker->getPosition();
	ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::BASIC, "new-horizons:armorer.pavise"));
	ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::ADVANCED, "new-horizons:armorer.formationFighting"));
	const auto * neighbor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(17), 10);
	resetChild();
	const auto actor = child->getForUpdate(incoming->unitId());
	const auto target = child->getForUpdate(braced->unitId());
	actor->setPosition(rearPosition);
	BattleAttackInfo attack(actor.get(), target.get(), 0, false);
	ASSERT_TRUE(expectOracle(*child, attack).ambusher);
	const auto contact = child->getForUpdate(neighbor->unitId());
	bool placed = false;
	for(const auto & hex : target->getSurroundingHexes())
		if(hex.isAvailable() && !child->battleGetUnitByPos(hex))
		{
			contact->setPosition(hex);
			placed = true;
			break;
		}
	ASSERT_TRUE(placed);
	ASSERT_TRUE(child->battleHasFormationFightingProtection(target.get(), target->getPosition()));
	EXPECT_EQ(expectOracle(*child, attack), BattleShroudStrike{});
	contact->setPosition(BattleHex(17));
	EXPECT_TRUE(expectOracle(*child, attack).ambusher);
}

TEST_F(BattleShroudStrikeTest, ProtectResolvesTheConcreteTargetBeforeStrikeFacts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 20);
	activate(braced->unitId());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			braced->unitId(), ward->unitId())));
	activate(incoming->unitId());
	ASSERT_NO_FATAL_FAILURE(selectShroud());
	const auto attack = rear(child);
	const auto * resolved = child->battleResolveHeroOrderTarget(attack.attacker, ward, false);
	ASSERT_EQ(resolved->unitId(), braced->unitId());
	auto concrete = BattleAttackInfo(attack.attacker, resolved, 0, false);
	concrete.protectIntercepted = true;
	EXPECT_EQ(expectOracle(*child, concrete), expectOracle(*child, attack));
	const auto before = snapshot(child);
	const auto orders = child->getHeroOrderStates(BattleSide::ATTACKER);
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::DEFENDER);
	const auto random = rng();
	BattleExchangeVariant readOnly;
	readOnly.trackAttack(child->getForUpdate(incoming->unitId()), child->getForUpdate(ward->unitId()),
		false, true, cache, child, true, true);
	EXPECT_EQ(snapshot(child), before);
	EXPECT_EQ(child->getHeroOrderStates(BattleSide::ATTACKER), orders);
	EXPECT_EQ(rng(), random);
}
}
