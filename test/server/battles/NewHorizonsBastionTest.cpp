/*
 * NewHorizonsBastionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vcmi/Environment.h>

namespace
{
constexpr auto armorerSkillId = "new-horizons:armorer";
constexpr auto pavisePerkId = "new-horizons:armorer.pavise";
constexpr auto veteranPerkId = "new-horizons:armorer.veteran";
constexpr auto bastionPerkId = "new-horizons:armorer.bastion";
constexpr auto bastionRoundKey = "armorerBastionRound";

class BastionTestEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit BastionTestEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

bool setPerkActive(JsonNode & rules, std::string_view perkId)
{
	auto & perks = rules["skills"][armorerSkillId]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class NewHorizonsBastionTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		// Bastion remains planned in production data; this saved-profile fixture activates it locally.
		if(!setPerkActive(perkRules, pavisePerkId)
			|| !setPerkActive(perkRules, veteranPerkId)
			|| !setPerkActive(perkRules, bastionPerkId))
			throw std::runtime_error("Missing Armorer prerequisite or Bastion perk from New Horizons rules");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view perkId,
		MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [perkId](const auto & candidate)
			{
				return candidate.selection.skillId == armorerSkillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			ASSERT_EQ(selected->requiredRank, static_cast<int>(requiredRank));
			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			EXPECT_TRUE(hero->hasActivePerk(std::string(armorerSkillId), std::string(perkId)));
			return;
		}

		FAIL() << perkId << " never appeared in a legal Armorer perk offer";
	}

	void selectBastion(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(armorerSkillId);
		ASSERT_GE(decoded, 0);
		const SecondarySkill armorer(decoded);

		hero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, pavisePerkId, MasteryLevel::BASIC);
		hero->setSecSkillLevel(armorer, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, veteranPerkId, MasteryLevel::ADVANCED);
		hero->setSecSkillLevel(armorer, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, bastionPerkId, MasteryLevel::EXPERT);
		ASSERT_TRUE(hero->hasActivePerk(std::string(armorerSkillId), std::string(bastionPerkId)));
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	std::vector<CStack *> addAdjacentAttackers(BattleSide side, const CreatureID & creature,
		const BattleHex & targetPosition, int32_t count, std::size_t number)
	{
		std::vector<CStack *> result;
		for(const auto & hex : BattleHexArray::getNeighbouringTiles(targetPosition))
		{
			if(result.size() == number)
				break;
			if(battle()->battleGetStackByPos(hex))
				continue;
			if(auto * stack = addStack(side, creature, hex, count))
				result.push_back(stack);
		}
		return result;
	}

	void expectDistinctAccessibleFootprints(const std::vector<const CStack *> & stacks) const
	{
		BattleHexArray occupiedHexes;
		for(const auto * stack : stacks)
		{
			ASSERT_NE(stack, nullptr);
			const auto & footprint = stack->getHexes();
			ASSERT_FALSE(footprint.empty());
			for(const auto & hex : footprint)
			{
				ASSERT_TRUE(hex.isAvailable()) << "Stack footprint must be on a legal battlefield tile";
				ASSERT_FALSE(occupiedHexes.contains(hex)) << "Stack footprints must not overlap at " << hex.toInt();
				EXPECT_EQ(battle()->battleGetStackByPos(hex), stack)
					<< "Every footprint hex must resolve to its owning stack";
				occupiedHexes.insert(hex);
			}
		}
	}

	void expectBastionDamageWithinFinalFloor(const int64_t reducedDamage, const int64_t unprotectedDamage) const
	{
		// Bastion multiplies before the damage calculator's final floor; flooring the baseline first can differ by one HP.
		const auto flooredBaseline = unprotectedDamage * 70 / 100;
		EXPECT_GE(reducedDamage, flooredBaseline - 1);
		EXPECT_LE(reducedDamage, flooredBaseline + 1);
	}

	bool defendThroughValidatedAction(CStack * stack)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(stack), BattleAction::makeDefend(stack));
	}

	std::optional<int64_t> damageFromLastAttack(const CStack * attacker, const CStack * defender) const
	{
		const auto attack = std::find_if(server.attacks.rbegin(), server.attacks.rend(), [attacker](const BattleAttack & result)
		{
			return result.stackAttacking == attacker->unitId() && !result.counter();
		});
		if(attack == server.attacks.rend())
			return std::nullopt;

		const auto hit = std::find_if(attack->bsa.begin(), attack->bsa.end(), [defender](const BattleStackAttacked & result)
		{
			return result.stackAttacked == defender->unitId();
		});
		if(hit == attack->bsa.end())
			return std::nullopt;
		return hit->damageAmount;
	}
};
}

TEST_F(NewHorizonsBastionTest, DefendingProtectionScalesOnlyTheFirstPhysicalAttackAndRenewsNextRound)
{
	startGame();
	selectBastion(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 1000);
	ASSERT_NE(defender, nullptr);
	auto attackers = addAdjacentAttackers(BattleSide::ATTACKER, creatureByName("core:pikeman"),
		defender->getPosition(), 100, 2);
	auto renewalAttackers = addAdjacentAttackers(BattleSide::ATTACKER, creatureByName("core:pikeman"),
		defender->getPosition(), 100, 2);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(8, 0), 1);
	ASSERT_EQ(attackers.size(), 2u);
	ASSERT_EQ(renewalAttackers.size(), 2u);
	ASSERT_NE(reserve, nullptr);
	std::vector<const CStack *> participants{defender, reserve};
	for(auto * attacker : attackers)
	{
		forceMaximumDamage(attacker);
		participants.push_back(attacker);
	}
	for(auto * attacker : renewalAttackers)
	{
		forceMaximumDamage(attacker);
		participants.push_back(attacker);
	}
	expectDistinctAccessibleFootprints(participants);
	blockRetaliation(defender);
	beginCombat();
	ASSERT_TRUE(defendThroughValidatedAction(defender));
	ASSERT_TRUE(defender->defended());

	const auto firstRound = battle()->getRound();
	ASSERT_TRUE(battle()->battleHasBastionProtection(defender));
	server.attacks.clear();
	ASSERT_TRUE(attack(attackers[0], defender->getPosition()));
	const auto firstDamage = damageFromLastAttack(attackers[0], defender);
	ASSERT_TRUE(firstDamage.has_value());
	EXPECT_EQ(defender->acquireState()->armorerBastionRound, firstRound);
	EXPECT_FALSE(battle()->battleHasBastionProtection(defender));
	EXPECT_TRUE(defender->defended());

	const BattleAttackInfo unspentFirstRoundIncoming(attackers[1], defender, 0, false);
	const auto unprotectedFirstRound = battle()->calculateDmgRange(unspentFirstRoundIncoming).damage.max;
	ASSERT_GT(unprotectedFirstRound, 0);
	server.attacks.clear();
	ASSERT_TRUE(attack(attackers[1], defender->getPosition()));
	const auto secondDamage = damageFromLastAttack(attackers[1], defender);
	ASSERT_TRUE(secondDamage.has_value());
	EXPECT_EQ(*secondDamage, unprotectedFirstRound);
	expectBastionDamageWithinFinalFloor(*firstDamage, unprotectedFirstRound);

	endRound();
	const auto nextRound = battle()->getRound();
	ASSERT_GT(nextRound, firstRound);
	ASSERT_TRUE(defendThroughValidatedAction(defender));
	ASSERT_TRUE(defender->defended());
	ASSERT_TRUE(battle()->battleHasBastionProtection(defender));
	server.attacks.clear();
	ASSERT_TRUE(attack(renewalAttackers[0], defender->getPosition()));
	const auto renewedFirstDamage = damageFromLastAttack(renewalAttackers[0], defender);
	ASSERT_TRUE(renewedFirstDamage.has_value());
	EXPECT_EQ(defender->acquireState()->armorerBastionRound, nextRound);

	const BattleAttackInfo unspentNextRoundIncoming(renewalAttackers[1], defender, 0, false);
	const auto unprotectedNextRound = battle()->calculateDmgRange(unspentNextRoundIncoming).damage.max;
	ASSERT_GT(unprotectedNextRound, 0);
	server.attacks.clear();
	ASSERT_TRUE(attack(renewalAttackers[1], defender->getPosition()));
	const auto renewedSecondDamage = damageFromLastAttack(renewalAttackers[1], defender);
	ASSERT_TRUE(renewedSecondDamage.has_value());
	EXPECT_EQ(*renewedSecondDamage, unprotectedNextRound);
	expectBastionDamageWithinFinalFloor(*renewedFirstDamage, unprotectedNextRound);
}

TEST_F(NewHorizonsBastionTest, NonDefendedAndSpellLikeAttacksDoNotSpendThePhysicalHit)
{
	startGame();
	selectBastion(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 1000);
	ASSERT_NE(defender, nullptr);
	auto physicalAttackers = addAdjacentAttackers(BattleSide::ATTACKER, creatureByName("core:pikeman"),
		defender->getPosition(), 1000, 3);
	auto * spellLikeAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(3, 5), 1);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(8, 0), 1);
	ASSERT_EQ(physicalAttackers.size(), 3u);
	ASSERT_NE(spellLikeAttacker, nullptr);
	ASSERT_NE(reserve, nullptr);
	ASSERT_TRUE(spellLikeAttacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	std::vector<const CStack *> participants{defender, spellLikeAttacker, reserve};
	for(auto * attacker : physicalAttackers)
	{
		forceMaximumDamage(attacker);
		participants.push_back(attacker);
	}
	expectDistinctAccessibleFootprints(participants);
	forceMaximumDamage(spellLikeAttacker);
	blockRetaliation(defender);
	beginCombat();

	const BattleAttackInfo physicalIncoming(physicalAttackers[0], defender, 0, false);
	const auto nonDefendedDamage = battle()->calculateDmgRange(physicalIncoming).damage.max;
	ASSERT_GT(nonDefendedDamage, 0);
	ASSERT_TRUE(attack(physicalAttackers[0], defender->getPosition()));
	const auto nonDefendedResult = damageFromLastAttack(physicalAttackers[0], defender);
	ASSERT_TRUE(nonDefendedResult.has_value());
	EXPECT_EQ(*nonDefendedResult, nonDefendedDamage);
	EXPECT_EQ(defender->acquireState()->armorerBastionRound, -1);

	ASSERT_TRUE(defendThroughValidatedAction(defender));
	ASSERT_TRUE(defender->defended());
	ASSERT_TRUE(battle()->battleHasBastionProtection(defender));
	server.attacks.clear();
	battle()->activeStack = spellLikeAttacker->unitId();
	const auto spellLikeShot = BattleAction::makeShotAttack(spellLikeAttacker, defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(spellLikeAttacker->unitSide()), spellLikeShot));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.back().spellLike());
	EXPECT_EQ(defender->acquireState()->armorerBastionRound, -1);
	EXPECT_TRUE(battle()->battleHasBastionProtection(defender));
	ASSERT_TRUE(physicalAttackers[1]->alive());
	ASSERT_TRUE(physicalAttackers[2]->alive());
	const auto protectedPrediction = battle()->calculateDmgRange(
		BattleAttackInfo(physicalAttackers[1], defender, 0, false)).damage.max;
	ASSERT_GT(protectedPrediction, 0);
	const auto countBeforeProtectedHit = physicalAttackers[1]->getCount();
	const auto countOfUnspentBaselineAttacker = physicalAttackers[2]->getCount();

	server.attacks.clear();
	ASSERT_TRUE(attack(physicalAttackers[1], defender->getPosition()));
	const auto firstProtectedDamage = damageFromLastAttack(physicalAttackers[1], defender);
	ASSERT_TRUE(firstProtectedDamage.has_value());
	EXPECT_EQ(defender->acquireState()->armorerBastionRound, battle()->getRound());
	EXPECT_EQ(*firstProtectedDamage, protectedPrediction)
		<< "Use the exact live prediction if the Lich splash left this attacker with a different count";
	const BattleAttackInfo unspentPhysicalIncoming(physicalAttackers[2], defender, 0, false);
	const auto unprotectedAfterSpend = battle()->calculateDmgRange(unspentPhysicalIncoming).damage.max;
	if(countBeforeProtectedHit == countOfUnspentBaselineAttacker)
		expectBastionDamageWithinFinalFloor(*firstProtectedDamage, unprotectedAfterSpend);
}

TEST_F(NewHorizonsBastionTest, HoldTheLineProtectsItsStacksWithoutMakingThemDefend)
{
	startGame();
	selectBastion(attackerSideHero);
	startBattle();
	removeDeployedUnits();
	auto * held = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 1000);
	ASSERT_NE(held, nullptr);
	auto attackers = addAdjacentAttackers(BattleSide::DEFENDER, creatureByName("core:pikeman"),
		held->getPosition(), 100, 2);
	auto * reserve = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(8, 0), 1);
	ASSERT_EQ(attackers.size(), 2u);
	ASSERT_NE(reserve, nullptr);
	std::vector<const CStack *> participants{held, reserve};
	for(auto * attacker : attackers)
	{
		forceMaximumDamage(attacker);
		participants.push_back(attacker);
	}
	expectDistinctAccessibleFootprints(participants);
	blockRetaliation(held);
	beginCombat();

	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_FALSE(held->defending);
	ASSERT_TRUE(battle()->battleHasBastionProtection(held));
	const auto protectedRound = battle()->getRound();
	server.attacks.clear();
	ASSERT_TRUE(attack(attackers[0], held->getPosition()));
	const auto firstDamage = damageFromLastAttack(attackers[0], held);
	ASSERT_TRUE(firstDamage.has_value());
	EXPECT_EQ(held->acquireState()->armorerBastionRound, protectedRound);

	const BattleAttackInfo unspentIncoming(attackers[1], held, 0, false);
	const auto unprotectedDamage = battle()->calculateDmgRange(unspentIncoming).damage.max;
	ASSERT_GT(unprotectedDamage, 0);
	server.attacks.clear();
	ASSERT_TRUE(attack(attackers[1], held->getPosition()));
	const auto secondDamage = damageFromLastAttack(attackers[1], held);
	ASSERT_TRUE(secondDamage.has_value());
	EXPECT_EQ(*secondDamage, unprotectedDamage);
	expectBastionDamageWithinFinalFloor(*firstDamage, unprotectedDamage);
}

TEST_F(NewHorizonsBastionTest, DetachedAttackProjectionSpendsItsFirstHitWithoutChangingLiveState)
{
	startGame();
	selectBastion(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 1000);
	ASSERT_NE(defender, nullptr);
	auto attackers = addAdjacentAttackers(BattleSide::ATTACKER, creatureByName("core:pikeman"),
		defender->getPosition(), 100, 2);
	ASSERT_EQ(attackers.size(), 2u);
	std::vector<const CStack *> participants{defender};
	for(auto * attacker : attackers)
	{
		forceMaximumDamage(attacker);
		participants.push_back(attacker);
	}
	expectDistinctAccessibleFootprints(participants);
	blockRetaliation(defender);
	beginCombat();
	ASSERT_TRUE(defendThroughValidatedAction(defender));
	battle()->activeStack = attackers[0]->unitId();

	const auto liveDefenderHealth = defender->getAvailableHealth();
	const auto liveFirstAttackerHealth = attackers[0]->getAvailableHealth();
	const auto liveSecondAttackerHealth = attackers[1]->getAvailableHealth();
	EXPECT_EQ(defender->acquireState()->armorerBastionRound, -1);

	BastionTestEnvironment environment(gameState());
	// The spectator callback is the authoritative mechanic oracle; a player-scoped view hides the opposing hero perks.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	ASSERT_NE(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projectedFirstAttacker = model->getForUpdate(attackers[0]->unitId());
	auto projectedDefender = model->getForUpdate(defender->unitId());
	const auto currentRound = battle()->getRound();
	ASSERT_TRUE(model->battleHasBastionProtection(projectedDefender.get()));

	DamageCache firstDamageCache;
	firstDamageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto first = AttackPossibility::evaluate(BattleAttackInfo(
		projectedFirstAttacker.get(), projectedDefender.get(), 0, false),
		projectedFirstAttacker->getPosition(), firstDamageCache, model);
	ASSERT_NE(first.effectPreview, nullptr);
	const auto firstTargetState = std::find_if(first.affectedUnits.begin(), first.affectedUnits.end(),
		[defender](const auto & unit) { return unit->unitId() == defender->unitId(); });
	ASSERT_NE(firstTargetState, first.affectedUnits.end());
	const auto firstProjectedDamage = projectedDefender->getAvailableHealth()
		- (*firstTargetState)->getAvailableHealth();
	ASSERT_GT(firstProjectedDamage, 0);
	EXPECT_EQ((*firstTargetState)->armorerBastionRound, currentRound);
	const auto * firstPreviewTarget = first.effectPreview->battleGetUnitByID(defender->unitId());
	ASSERT_NE(firstPreviewTarget, nullptr);
	EXPECT_FALSE(first.effectPreview->battleHasBastionProtection(firstPreviewTarget));
	EXPECT_EQ(defender->acquireState()->armorerBastionRound, -1)
		<< "Evaluating a forecast must not spend the authoritative marker";

	BattleExchangeVariant exchange;
	exchange.trackAttack(first, model, firstDamageCache);
	auto committedDefender = model->getForUpdate(defender->unitId());
	EXPECT_EQ(committedDefender->armorerBastionRound, currentRound)
		<< "The detached exchange replay carries the first-hit marker forward";
	EXPECT_FALSE(model->battleHasBastionProtection(committedDefender.get()));

	DamageCache secondDamageCache;
	secondDamageCache.buildDamageCache(model, BattleSide::ATTACKER);
	auto projectedSecondAttacker = model->getForUpdate(attackers[1]->unitId());
	const auto defenderHealthBeforeSecond = committedDefender->getAvailableHealth();
	const auto second = AttackPossibility::evaluate(BattleAttackInfo(
		projectedSecondAttacker.get(), committedDefender.get(), 0, false),
		projectedSecondAttacker->getPosition(), secondDamageCache, model);
	const auto secondTargetState = std::find_if(second.affectedUnits.begin(), second.affectedUnits.end(),
		[defender](const auto & unit) { return unit->unitId() == defender->unitId(); });
	ASSERT_NE(secondTargetState, second.affectedUnits.end());
	const auto secondProjectedDamage = defenderHealthBeforeSecond
		- (*secondTargetState)->getAvailableHealth();
	ASSERT_GT(secondProjectedDamage, 0);
	EXPECT_EQ((*secondTargetState)->armorerBastionRound, currentRound);
	expectBastionDamageWithinFinalFloor(firstProjectedDamage, secondProjectedDamage);

	EXPECT_EQ(defender->getAvailableHealth(), liveDefenderHealth);
	EXPECT_EQ(attackers[0]->getAvailableHealth(), liveFirstAttackerHealth);
	EXPECT_EQ(attackers[1]->getAvailableHealth(), liveSecondAttackerHealth);
	EXPECT_EQ(defender->acquireState()->armorerBastionRound, -1)
		<< "Projected first-hit consumption remains on the detached branch";
}

TEST_F(NewHorizonsBastionTest, UnitStateCopyAndLegacyJsonPreserveCompatibleRoundDefaults)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 10);
	ASSERT_NE(stack, nullptr);

	auto state = stack->acquireState();
	EXPECT_EQ(state->armorerBastionRound, -1);
	state->armorerBastionRound = 3;
	const auto saved = state->save();
	EXPECT_EQ(saved["state"][bastionRoundKey].Integer(), 3);

	auto copied = stack->acquireState();
	*copied = *state;
	EXPECT_EQ(copied->armorerBastionRound, 3);

	auto restored = stack->acquireState();
	restored->load(saved);
	EXPECT_EQ(restored->armorerBastionRound, 3);

	auto legacy = saved;
	legacy["state"].Struct().erase(bastionRoundKey);
	restored->load(legacy);
	EXPECT_EQ(restored->armorerBastionRound, -1)
		<< "Older unit-state JSON defaults the appended round marker to unused";

	auto invalid = saved;
	invalid["state"][bastionRoundKey] = JsonNode(-2);
	EXPECT_THROW(restored->load(invalid), std::runtime_error);
}
