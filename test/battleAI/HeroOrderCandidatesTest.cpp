/*
 * HeroOrderCandidatesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/HeroOrderCandidates.h"
#include "../../AI/BattleAI/HeroOrderCandidateRules.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <limits>
#include <tuple>

namespace
{
class CandidateCallback final : public CBattleCallback
{
public:
	size_t reads = 0;
	CandidateCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	std::shared_ptr<CPlayerBattleCallback> getBattle(const BattleID & id) override
	{
		++reads;
		return CBattleCallback::getBattle(id);
	}
};

class FixedValuation final : public heroOrderCandidates::Valuation
{
public:
	using Request = std::tuple<HeroCommand, std::vector<uint32_t>, std::optional<int>>;
	mutable std::vector<Request> requests;
	float value = 1.0f;
	float score(const heroOrderCandidates::ValuationRequest & request) const override
	{
		requests.emplace_back(request.command, request.targets, request.focusFirePercent);
		return value;
	}
};

template<typename T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

// Deliberately older callback shapes, so the production requires branches must
// retain their precedence and atomic tuple validation rather than scalar guessing.
struct ScalarOnly
{
	mutable size_t confirmations = 0;
	bool battleCanUseHeroCommand(BattleSide, HeroCommand) const { return true; }
	bool battleCanConfirmHeroCommand(BattleSide, HeroCommand, uint32_t id) const
	{
		++confirmations;
		return id == 7;
	}
};

struct TupleConfirmation : ScalarOnly
{
	bool battleCanConfirmHeroCommand(BattleSide, HeroCommand, const std::vector<uint32_t> & ids) const
	{
		++confirmations;
		return ids == std::vector<uint32_t>({7, 9});
	}
};

struct TupleOptions : TupleConfirmation
{
	std::vector<std::vector<uint32_t>> battleGetHeroCommandTargetOptions(BattleSide, HeroCommand) const
	{
		return {{9, 7}};
	}
};
}

TEST(HeroOrderCandidateLegacyAPI, ScalarCannotAuthorizeProtectPair)
{
	ScalarOnly view;
	EXPECT_FALSE(heroOrderCandidates::commandTargetIsLegal(view, BattleSide::ATTACKER,
		HeroCommand::PROTECT, {7, 9}));
	EXPECT_EQ(view.confirmations, 0u);
	EXPECT_TRUE(heroOrderCandidates::commandTargetIsLegal(view, BattleSide::ATTACKER,
		HeroCommand::FOCUS_FIRE, {7}));
	EXPECT_EQ(view.confirmations, 1u);
}

TEST(HeroOrderCandidateLegacyAPI, CompleteTupleOptionsPrecedeConfirmation)
{
	TupleOptions view;
	EXPECT_FALSE(heroOrderCandidates::commandTargetIsLegal(view, BattleSide::ATTACKER,
		HeroCommand::PROTECT, {7, 9}));
	EXPECT_TRUE(heroOrderCandidates::commandTargetIsLegal(view, BattleSide::ATTACKER,
		HeroCommand::PROTECT, {9, 7}));
	EXPECT_EQ(view.confirmations, 0u);
}

TEST(HeroOrderCandidateLegacyAPI, TupleConfirmationRetainsProtectorWardOrder)
{
	TupleConfirmation view;
	EXPECT_TRUE(heroOrderCandidates::commandTargetIsLegal(view, BattleSide::ATTACKER,
		HeroCommand::PROTECT, {7, 9}));
	EXPECT_FALSE(heroOrderCandidates::commandTargetIsLegal(view, BattleSide::ATTACKER,
		HeroCommand::PROTECT, {9, 7}));
}

class HeroOrderCandidatesTest : public HeroCommandFixture
{
protected:
	CandidateCallback callback;
	FixedValuation valuation;
	bool legacy = false;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(legacy)
		{
			const JsonNode oldRules(JsonPath::builtin("config/newHorizonsCombatV2"));
			map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, oldRules["combat"]["heroCommands"]);
		}
	}

	void prepare(bool shooter = true)
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		auto * own = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 10);
		ASSERT_NE(own, nullptr);
		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(69), 10), nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(75), 10), nullptr);
		if(shooter)
		{
			own->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID()));
			own->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::SHOTS, BonusSource::OTHER, 10, BonusSourceID()));
		}
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = own->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback.onBattleStarted(battle());
	}

	std::vector<PossibleSpellcast> collect(bool mandatory = false)
	{
		std::vector<PossibleSpellcast> result;
		heroOrderCandidates::appendOrders(result, callback, BattleID(0), BattleSide::ATTACKER, mandatory, valuation);
		return result;
	}
};

TEST_F(HeroOrderCandidatesTest, AppendsAfterSpellPrefixAndKeepsExactCommandTargetSequence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto view = callback.getBattle(BattleID(0));
	std::vector<std::pair<HeroCommand, std::vector<uint32_t>>> expected;
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::RIPOSTE, HeroCommand::BRACE})
		if(view->battleCanUseHeroCommand(BattleSide::ATTACKER, command))
			expected.emplace_back(command, std::vector<uint32_t>{});
	for(auto command : {HeroCommand::FOCUS_FIRE, HeroCommand::PROTECT, HeroCommand::FLANK, HeroCommand::SECOND_WIND})
	{
		const auto ids = view->battleGetHeroCommandTargets(BattleSide::ATTACKER, command);
		for(auto first : ids)
		{
			if(command == HeroCommand::PROTECT)
			{
				for(auto second : ids)
					if(first != second && view->battlePrepareHeroOrderState(BattleSide::ATTACKER, command, {first, second}))
						expected.emplace_back(command, std::vector<uint32_t>{first, second});
			}
			else if(view->battlePrepareHeroOrderState(BattleSide::ATTACKER, command, {first}))
				expected.emplace_back(command, std::vector<uint32_t>{first});
		}
	}
	ASSERT_GT(expected.size(), 4u);
	std::vector<PossibleSpellcast> result(1);
	result.front().spellOvercharge = 913;
	const auto readsBefore = callback.reads;
	heroOrderCandidates::appendOrders(result, callback, BattleID(0), BattleSide::ATTACKER, false, valuation);
	ASSERT_EQ(result.size(), expected.size() + 1);
	const auto targeted = std::count_if(expected.begin(), expected.end(), [](const auto & choice)
	{
		return !choice.second.empty();
	});
	const auto focus = std::count_if(expected.begin(), expected.end(), [](const auto & choice)
	{
		return choice.first == HeroCommand::FOCUS_FIRE;
	});
	EXPECT_EQ(callback.reads - readsBefore, 8u + 2u * targeted + focus);
	EXPECT_EQ(result.front().spellOvercharge, 913);
	for(size_t i = 0; i < expected.size(); ++i)
	{
		EXPECT_EQ(result[i+1].command, expected[i].first);
		EXPECT_EQ(result[i+1].commandTargets, expected[i].second);
		if(result[i+1].command == HeroCommand::FOCUS_FIRE)
		{
			ASSERT_TRUE(result[i+1].focusFire);
			const auto prepared = view->battlePrepareFocusFireState(BattleSide::ATTACKER, expected[i].second.front());
			ASSERT_TRUE(prepared);
			EXPECT_EQ(serialized(*result[i+1].focusFire), serialized(*prepared));
		}
	}
}

TEST_F(HeroOrderCandidatesTest, NonpositiveOptionalForecastsDropButMandatoryAndNaNRemain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	valuation.value = -3.0f;
	EXPECT_TRUE(collect().empty());
	const auto mandatory = collect(true);
	ASSERT_FALSE(mandatory.empty());
	for(const auto & candidate : mandatory)
		EXPECT_FLOAT_EQ(candidate.commandHeuristicValue, -3.0f);
	valuation.value = std::numeric_limits<float>::quiet_NaN();
	const auto nan = collect();
	ASSERT_EQ(nan.size(), mandatory.size());
	for(size_t i = 0; i < nan.size(); ++i)
	{
		EXPECT_EQ(nan[i].command, mandatory[i].command);
		EXPECT_EQ(nan[i].commandTargets, mandatory[i].commandTargets);
	}
}

TEST_F(HeroOrderCandidatesTest, EnumerationLeavesPreparedSnapshotsAllowancesRngAndTrafficExact)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto beforeLedger = serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER));
	const auto beforeOrders = serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER));
	const auto beforeRng = serialized(*gameHandler->randomizer);
	const auto beforeTraffic = server.startedActions.size();
	const auto before = collect();
	const auto after = collect();
	ASSERT_FALSE(before.empty());
	ASSERT_EQ(before.size(), after.size());
	for(size_t i = 0; i < before.size(); ++i)
	{
		EXPECT_EQ(before[i].command, after[i].command);
		EXPECT_EQ(before[i].commandTargets, after[i].commandTargets);
		EXPECT_EQ(before[i].focusFire.has_value(), after[i].focusFire.has_value());
		if(before[i].focusFire)
			EXPECT_EQ(serialized(*before[i].focusFire), serialized(*after[i].focusFire));
	}
	EXPECT_EQ(serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER)), beforeLedger);
	EXPECT_EQ(serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER)), beforeOrders);
	EXPECT_EQ(serialized(*gameHandler->randomizer), beforeRng);
	EXPECT_EQ(server.startedActions.size(), beforeTraffic);
}

TEST_F(HeroOrderCandidatesTest, FocusFireWithoutRemainingShooterHasNoCandidate)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto result = collect(true);
	ASSERT_FALSE(result.empty());
	EXPECT_TRUE(std::none_of(result.begin(), result.end(), [](const auto & candidate)
	{
		return candidate.command == HeroCommand::FOCUS_FIRE;
	}));
}

TEST_F(HeroOrderCandidatesTest, LegacySideWideBypassesValuationWhileFocusFireUsesPreparedPercent)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	valuation.value = -1.0f;
	const auto optional = collect();
	ASSERT_FALSE(optional.empty());
	for(const auto & candidate : optional)
	{
		EXPECT_NE(candidate.command, HeroCommand::FOCUS_FIRE);
		EXPECT_FLOAT_EQ(candidate.commandHeuristicValue, 0.0f);
	}
	ASSERT_FALSE(valuation.requests.empty());
	for(const auto & [command, targets, percent] : valuation.requests)
	{
		EXPECT_EQ(command, HeroCommand::FOCUS_FIRE);
		EXPECT_EQ(targets.size(), 1u);
		EXPECT_TRUE(percent);
	}
	const auto mandatory = collect(true);
	EXPECT_TRUE(std::any_of(mandatory.begin(), mandatory.end(), [](const auto & candidate)
	{
		return candidate.command == HeroCommand::FOCUS_FIRE && candidate.focusFire.has_value();
	}));
}
