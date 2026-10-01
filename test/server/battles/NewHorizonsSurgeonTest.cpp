/*
 * NewHorizonsSurgeonTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/BattleAI.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr auto warMachinesSkillId = "new-horizons:warMachines";
constexpr auto surgeonPerkId = "new-horizons:warMachines.surgeon";

bool setPerkActive(JsonNode & rules, std::string_view skillId, std::string_view perkId)
{
	auto & perks = rules["skills"][std::string(skillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class SurgeonTestEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SurgeonTestEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

Bonus makeMarker(const std::string & kind, const int64_t applicationOrder,
	const BonusSource source, const BonusSourceID & sourceID)
{
	Bonus result(BonusDuration::PERMANENT, BonusType::PHYSICAL_AFFLICTION, source, 0, sourceID);
	JsonNode parameters;
	parameters["kind"].String() = kind;
	parameters["applicationOrder"].Integer() = applicationOrder;
	result.parameters = std::make_shared<BonusParameters>(parameters);
	return result;
}

std::vector<Bonus> makeMarkedGroup(const std::string & kind, const int64_t applicationOrder,
	const BonusSourceID & sourceID)
{
	Bonus effect(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, -1, sourceID);
	return {effect, makeMarker(kind, applicationOrder, BonusSource::OTHER, sourceID)};
}

bool hasAffliction(const battle::Unit & unit, const std::string & kind)
{
	const auto afflictions = physicalAfflictions::enumerate(unit);
	return std::any_of(afflictions.begin(), afflictions.end(), [&](const auto & affliction)
	{
		return affliction.kind == kind;
	});
}
}

class NewHorizonsSurgeonTest : public BattleTestFixture
{
protected:
	const CStack * tent = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setPerkActive(perkRules, warMachinesSkillId, surgeonPerkId))
			throw std::runtime_error("Missing Surgeon from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
		// Preserve the canonical v4 shop/progression fields while customizing First Aid output.
		capabilities["siege"]["siegeRating"].Vector().clear();
		capabilities["siege"]["directControlChance"].Vector().clear();
		for(const int value : {0, 20, 40, 60})
			capabilities["siege"]["siegeRating"].Vector().emplace_back(value);
		for(const int value : {0, 100, 100, 100})
			capabilities["siege"]["directControlChance"].Vector().emplace_back(value);
		capabilities["siege"]["outputs"]["firstAidHealing"]["base"].Integer() = 40;
		capabilities["siege"]["outputs"]["firstAidHealing"]["siegeCoefficientHalf"].Integer() = 0;
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, std::move(capabilities));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, const std::string_view skillId,
		const std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & queriedSkillId)
		{
			return hero->getPerkSkillRank(queriedSkillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&](const auto & candidate)
			{
				return candidate.selection.skillId == skillId && candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<std::size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(skillId), std::string(perkId)));
			return;
		}

		FAIL() << perkId << " never appeared in a legal perk offer";
	}

	void setWarMachines(CGHeroInstance * hero, const bool surgeon)
	{
		const int decoded = SecondarySkill::decode(std::string(warMachinesSkillId));
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(surgeon)
			acceptPerkThroughOffer(hero, warMachinesSkillId, surgeonPerkId);
	}

	void prepareBattle(const bool surgeon)
	{
		startGame();
		attackerSideHero->setSecSkillLevel(SecondarySkill::FIRST_AID,
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		setWarMachines(attackerSideHero, surgeon);
		giveArtifact(attackerSideHero, ArtifactID::FIRST_AID_TENT, ArtifactPosition::MACH3);
		startBattle();

		const auto tents = battle()->battleGetStacksIf([](const CStack * stack)
		{
			return stack->unitSide() == BattleSide::ATTACKER && stack->isFirstAidTent();
		});
		ASSERT_EQ(tents.size(), 1u);
		tent = tents.front();
		ASSERT_EQ(battle()->battleGetOwnerHero(tent), attackerSideHero);
		ASSERT_EQ(tent->unitOwner(), battle()->battleGetOwner(tent));
		if(surgeon)
			ASSERT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, surgeonPerkId));
		else
			ASSERT_FALSE(attackerSideHero->hasActivePerk(warMachinesSkillId, surgeonPerkId));
	}

	CStack * addFriendlyAzureDragon()
	{
		return addStack(BattleSide::ATTACKER, creatureByName("core:azureDragon"), BattleHex(leftHex), 1);
	}

	void addMarkedAffliction(CStack * target, const std::string & kind, const int64_t applicationOrder,
		const int32_t sourceNumber)
	{
		const BonusSourceID sourceID{BonusCustomSource(sourceNumber)};
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(target->unitId(), makeMarkedGroup(kind, applicationOrder, sourceID));
		gameHandler->sendAndApply(effect);
	}

	void wound(CStack * target, const int64_t damage)
	{
		ASSERT_GT(damage, 0);
		StacksInjured injury;
		injury.battleID = BattleID(0);
		auto & attacked = injury.stacks.emplace_back();
		attacked.stackAttacked = target->unitId();
		attacked.damageAmount = damage;
		target->prepareAttacked(attacked, gameHandler->getRandomGenerator());
		ASSERT_EQ(attacked.killedAmount, 0u);
		gameHandler->sendAndApply(injury);
		ASSERT_TRUE(target->alive());
		ASSERT_TRUE(target->canBeHealed());
	}

	void addStoredPoison(CStack * target)
	{
		auto state = target->acquireState();
		state->physicalPoisonBaseDamage = 5;
		state->physicalPoisonActivationsRemaining = 3;
		state->physicalPoisonSourceStackId = 17;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(state->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	bool advanceUntilTent()
	{
		for(size_t turn = 0; turn < battle()->stacks.size() * 4; ++turn)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == tent->unitId())
				return true;
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	void healWithTent(CStack * target)
	{
		ASSERT_TRUE(advanceUntilTent());
		ASSERT_EQ(battle()->battleActiveUnit()->unitId(), tent->unitId());
		const auto healthBefore = target->getAvailableHealth();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(tent->unitSide()), BattleAction::makeHeal(tent, target)));
		EXPECT_GT(target->getAvailableHealth(), healthBefore)
			<< "Surgeon may cleanse only after actual healing increases the target's HP";
	}
};

TEST_F(NewHorizonsSurgeonTest, WoundedFriendlyTentCleansesExactlyOneAfflictionInDocumentedPriorityOrder)
{
	prepareBattle(true);
	auto * target = addFriendlyAzureDragon();
	ASSERT_NE(target, nullptr);

	// Synthetic generic marked statuses exercise the marker contract; they do not claim
	// that any particular production spell or creature currently produces Bleeding/other.
	addMarkedAffliction(target, "other", 1, 1801);
	addMarkedAffliction(target, "bleeding", 2, 1802);
	addMarkedAffliction(target, "other", 3, 1803);
	addMarkedAffliction(target, "disease", 4, 1804);
	addMarkedAffliction(target, "poison", 5, 1805);

	// The healing forecast is state-dependent and returns zero for an unwounded target.
	const int64_t expectedHealingPerActivation = 40;
	const auto damage = std::min<int64_t>(target->getMaxHealth() - 1, expectedHealingPerActivation * 4 + 1);
	wound(target, damage);
	const auto healing = battle()->getFirstAidHealValue(attackerSideHero, target);
	ASSERT_EQ(healing, 40);
	ASSERT_EQ(physicalAfflictions::enumerate(*target).size(), 5u);
	ASSERT_EQ(physicalAfflictions::first(*target)->kind, "poison");
	ASSERT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, surgeonPerkId));

	beginCombat();
	const auto owner = battle()->battleGetOwnerHero(tent);
	ASSERT_EQ(owner, attackerSideHero);
	ASSERT_TRUE(owner->hasActivePerk(warMachinesSkillId, surgeonPerkId));

	// Exercise the production AI selection hook against the same live status catalogue.
	auto environment = std::make_shared<SurgeonTestEnvironment>(gameState());
	auto callback = std::make_shared<CBattleCallback>(battle()->sideToPlayer(tent->unitSide()), nullptr);
	callback->onBattleStarted(battle());
	CBattleAI ai;
	ai.initBattleInterface(environment, callback);
	const auto aiAction = ai.useHealingTent(BattleID(0), tent);
	EXPECT_EQ(aiAction.actionType, EActionType::STACK_HEAL);
	const auto aiTarget = aiAction.getTarget(battle());
	ASSERT_EQ(aiTarget.size(), 1u);
	EXPECT_EQ(aiTarget.front().unitValue, target);

	const std::vector<std::string> expectedOrder{"poison", "disease", "bleeding", "other"};
	for(size_t index = 0; index < expectedOrder.size(); ++index)
	{
		SCOPED_TRACE(expectedOrder[index]);
		const auto selected = physicalAfflictions::first(*target);
		ASSERT_TRUE(selected);
		EXPECT_EQ(selected->kind, expectedOrder[index]);
		const auto countBefore = physicalAfflictions::enumerate(*target).size();
		healWithTent(target);
		const auto remaining = physicalAfflictions::enumerate(*target);
		ASSERT_EQ(remaining.size(), countBefore - 1);
		EXPECT_FALSE(std::any_of(remaining.begin(), remaining.end(), [&](const auto & affliction)
		{
			return affliction.source == selected->source && affliction.sourceID == selected->sourceID;
		}));
		if(index + 1 < expectedOrder.size())
			ASSERT_EQ(physicalAfflictions::first(*target)->kind, expectedOrder[index + 1]);
	}

	const auto remaining = physicalAfflictions::enumerate(*target);
	ASSERT_EQ(remaining.size(), 1u);
	EXPECT_EQ(remaining.front().kind, "other");
	EXPECT_EQ(remaining.front().applicationOrder, 3);
}

TEST_F(NewHorizonsSurgeonTest, BasicWarMachinesWithoutSurgeonHealsButLeavesMarkedStatus)
{
	prepareBattle(false);
	auto * target = addFriendlyAzureDragon();
	ASSERT_NE(target, nullptr);
	addMarkedAffliction(target, "bleeding", 1, 1810);
	wound(target, 1);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(warMachinesSkillId, surgeonPerkId));

	beginCombat();
	healWithTent(target);
	const auto afflictions = physicalAfflictions::enumerate(*target);
	ASSERT_EQ(afflictions.size(), 1u);
	EXPECT_EQ(afflictions.front().kind, "bleeding");
}

TEST_F(NewHorizonsSurgeonTest, FailedNoHealAndEnemyTargetActionsNeverCleanse)
{
	prepareBattle(true);
	auto * healthyAlly = addFriendlyAzureDragon();
	auto * woundedLegalAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 2), 10);
	auto * woundedEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:azureDragon"), BattleHex(rightHex), 1);
	ASSERT_NE(healthyAlly, nullptr);
	ASSERT_NE(woundedLegalAlly, nullptr);
	ASSERT_NE(woundedEnemy, nullptr);
	addMarkedAffliction(healthyAlly, "other", 1, 1820);
	addMarkedAffliction(woundedEnemy, "bleeding", 2, 1821);
	wound(woundedLegalAlly, 1);
	wound(woundedEnemy, 1);
	ASSERT_FALSE(healthyAlly->canBeHealed());
	ASSERT_TRUE(woundedLegalAlly->canBeHealed());
	ASSERT_TRUE(woundedEnemy->canBeHealed());
	beginCombat();

	ASSERT_TRUE(advanceUntilTent());
	BattleAction missingTarget;
	missingTarget.actionType = EActionType::STACK_HEAL;
	missingTarget.side = BattleSide::ATTACKER;
	missingTarget.stackNumber = tent->unitId();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(tent->unitSide()), missingTarget));
	EXPECT_TRUE(hasAffliction(*healthyAlly, "other"));

	ASSERT_TRUE(advanceUntilTent());
	const auto healthyBefore = healthyAlly->getAvailableHealth();
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(tent->unitSide()), BattleAction::makeHeal(tent, healthyAlly)));
	EXPECT_EQ(healthyAlly->getAvailableHealth(), healthyBefore);
	EXPECT_TRUE(hasAffliction(*healthyAlly, "other"));

	ASSERT_TRUE(advanceUntilTent());
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(tent->unitSide()), BattleAction::makeHeal(tent, woundedEnemy)));
	EXPECT_TRUE(hasAffliction(*woundedEnemy, "bleeding"));
}

TEST_F(NewHorizonsSurgeonTest, SuccessfulHealingClearsStoredPhysicalPoisonFromAuthoritativeUnitState)
{
	prepareBattle(true);
	auto * target = addFriendlyAzureDragon();
	ASSERT_NE(target, nullptr);
	addStoredPoison(target);
	wound(target, 1);
	const auto stored = physicalAfflictions::first(*target);
	ASSERT_TRUE(stored);
	ASSERT_TRUE(stored->storedPoison);

	beginCombat();
	healWithTent(target);
	EXPECT_TRUE(physicalAfflictions::enumerate(*target).empty());
	const auto cleared = target->acquireState();
	EXPECT_EQ(cleared->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(cleared->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(cleared->physicalPoisonSourceStackId, -1);
}
