/*
 * OrderTransitionCharacterizationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsCrisisCommand.h"
#include "../../lib/battle/NewHorizonsHeroLuck.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>

#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace
{
using Ledger = HeroActionAllowanceState;
using Action = Ledger::ActionKind;
using Allowance = Ledger::AllowanceKind;
using Source = Ledger::GrantSource;
constexpr auto SIDE = BattleSide::ATTACKER;

std::string hex(const std::vector<std::byte> & bytes)
{
	constexpr auto digits = "0123456789abcdef";
	std::string result;
	for(const auto entry : bytes)
	{
		const auto byte = std::to_integer<unsigned char>(entry);
		result += digits[byte >> 4]; result += digits[byte & 15];
	}
	return result;
}

template<typename Value>
std::string wire(Value value)
{
	CMemorySerializer memory; memory.oser & value;
	return hex(memory.extractBuffer());
}

std::string wire(GameRandomizer & value)
{
	CMemorySerializer memory; memory.oser & value;
	return hex(memory.extractBuffer());
}

JsonNode ids(const std::vector<uint32_t> & values)
{
	JsonNode result;
	result.Vector();
	for(const auto value : values) result.Vector().emplace_back(static_cast<int64_t>(value));
	return result;
}

JsonNode receipt(const Ledger::Receipt & value)
{
	JsonNode result;
	result["grantId"] = JsonNode(static_cast<int64_t>(value.grantId));
	result["action"] = JsonNode(static_cast<int>(value.action));
	result["allowance"] = JsonNode(static_cast<int>(value.allowance));
	result["source"] = JsonNode(static_cast<int>(value.source));
	result["round"] = JsonNode(value.round);
	result["recipients"] = ids(value.divineMandateRecipients);
	return result;
}

JsonNode continuation(const DoubleCommandState & value)
{
	// Include malformed inputs too: their canonical serializer correctly refuses.
	JsonNode result;
	result["used"] = JsonNode(value.used);
	result["phase"] = JsonNode(static_cast<int>(value.phase));
	result["firstOrder"] = JsonNode(static_cast<int>(value.firstOrder));
	result["round"] = JsonNode(value.issuedRound);
	result["anchor"] = JsonNode(static_cast<int64_t>(value.anchorStackId));
	result["deferredTarget"] = JsonNode(static_cast<int64_t>(value.deferredSecondWindTargetUnitId));
	return result;
}

JsonNode outcome(const std::function<void()> & operation)
{
	JsonNode result;
	result["exception"] = JsonNode("none"); result["message"] = JsonNode("");
	try { operation(); }
	catch(const std::invalid_argument & error)
	{
		result["exception"] = JsonNode("std::invalid_argument"); result["message"] = JsonNode(error.what());
	}
	catch(const std::overflow_error & error)
	{
		result["exception"] = JsonNode("std::overflow_error"); result["message"] = JsonNode(error.what());
	}
	catch(const std::runtime_error & error)
	{
		result["exception"] = JsonNode("std::runtime_error"); result["message"] = JsonNode(error.what());
	}
	return result;
}

void publish(const JsonNode & rows)
{
	const auto text = rows.toCompactString();
	ASSERT_LE(text.size(), 4u * 1024u * 1024u);
	::testing::Test::RecordProperty("transitionRows", text);
}

Ledger singleGrant(Allowance allowance, Source source)
{
	Ledger ledger; ledger.resetForRound(1);
	if(source == Source::ROUND) return ledger;
	if(source == Source::HEROIC_SPIRIT)
	{
		const auto paid = ledger.consumeAllowance(1, Action::ORDER, 1);
		if(!paid || !ledger.resolveHeroicSpirit(*paid, true)) throw std::runtime_error("Fixture Heroic Spirit receipt");
	}
	else
	{
		ledger.grants.clear();
		ledger.grantAllowance(allowance, source, 1);
	}
	ledger.validateShape();
	return ledger;
}

newHorizonsCrisisCommand::State crisisChoice(bool choosing, uint32_t grant)
{
	newHorizonsCrisisCommand::State state;
	if(choosing)
	{
		state.used[0] = true;
		newHorizonsCrisisCommand::ReturnFrame frame;
		frame.responder = SIDE; frame.anchor = 7; frame.grant = grant; frame.round = 1;
		state.returns.push_back(frame);
	}
	state.validateShape();
	return state;
}

class OrderEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit OrderEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class OrderTransitionAICharacterizationTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}

	void prepare()
	{
		prepareCommands();
		ASSERT_FALSE(heroCommands::hasDoubleCommand(attackerSideHero));
		ASSERT_FALSE(battle()->battleGetDivineMandateStatus(SIDE).active);
		ASSERT_EQ(battle()->getRound(), 1);
	}
};
}

TEST(OrderTransitionCharacterizationTest, SourcePaymentMatrixKeepsCrisisOnlyAndMandateConjunctionDistinct)
{
	const std::array grants{std::pair{Allowance::HERO, Source::ROUND},
		std::pair{Allowance::HERO, Source::PERK}, std::pair{Allowance::HERO, Source::ARTIFACT},
		std::pair{Allowance::HERO, Source::OTHER}, std::pair{Allowance::ORDER, Source::PERK},
		std::pair{Allowance::ORDER, Source::ARTIFACT}, std::pair{Allowance::ORDER, Source::OTHER},
		std::pair{Allowance::ORDER, Source::DOUBLE_COMMAND}, std::pair{Allowance::ORDER, Source::BATTLE_PLAN},
		std::pair{Allowance::ORDER, Source::DIVINE_MANDATE}, std::pair{Allowance::ORDER, Source::CRISIS_COMMAND},
		std::pair{Allowance::ORDER, Source::HEROIC_SPIRIT}, std::pair{Allowance::SPELL, Source::METAMAGIC},
		std::pair{Allowance::SPELL, Source::METAMAGIC_GRAND}, std::pair{Allowance::SPELL, Source::PERK},
		std::pair{Allowance::SPELL, Source::ARTIFACT}, std::pair{Allowance::SPELL, Source::OTHER},
		std::pair{Allowance::SPELL, Source::DIVINE_MANDATE}};
	JsonNode rows;
	for(const auto & [allowance, source] : grants)
		for(const auto side : {SIDE, BattleSide::DEFENDER})
			for(const bool choice : {false, true})
				for(const bool mandate : {false, true})
					for(const bool combined : {false, true})
						for(const int round : {1, 2})
						{
							const auto before = singleGrant(allowance, source);
							const auto crisis = crisisChoice(choice, before.grants.front().id);
							const auto filter = [&](const Ledger::Grant & grant)
							{
								return crisis.allowsGrant(side, grant)
									&& (!combined || grant.source != Source::DIVINE_MANDATE || mandate);
							};
							const bool allowed = round == 1 && allowance != Allowance::SPELL
								&& (choice ? side == SIDE && source == Source::CRISIS_COMMAND : source != Source::CRISIS_COMMAND)
								&& (!combined || source != Source::DIVINE_MANDATE || mandate);
							const auto selected = before.eligibleAllowance(Action::ORDER, round, filter);
							EXPECT_EQ(selected.has_value(), allowed);
							auto next = before;
							const auto paid = next.consumeAllowance(before.grants.front().id, Action::ORDER, round, filter);
							EXPECT_EQ(paid.has_value(), allowed);
							if(paid)
							{
								EXPECT_EQ(*paid, (Ledger::Receipt{selected->grantId, Action::ORDER, allowance, source, round, {}}));
								EXPECT_TRUE(next.grants.empty());
							}
							else EXPECT_EQ(next, before);
							auto expectedNext = before;
							if(paid) expectedNext.grants.clear();
							EXPECT_EQ(next, expectedNext);
							JsonNode row;
							row["source"] = JsonNode(static_cast<int>(source)); row["allowance"] = JsonNode(static_cast<int>(allowance));
							row["side"] = JsonNode(static_cast<int>(side)); row["choice"] = JsonNode(choice);
							row["mandate"] = JsonNode(mandate); row["combined"] = JsonNode(combined); row["round"] = JsonNode(round);
							row["before"] = JsonNode(wire(before)); row["after"] = JsonNode(wire(next));
							row["selected"] = JsonNode(selected.has_value());
							row["selectedExpiry"] = JsonNode(selected ? selected->expiryRound : -1);
							row["receipt"] = paid ? receipt(*paid) : JsonNode(); rows.Vector().push_back(row);
						}
	ASSERT_EQ(rows.Vector().size(), 576u); publish(rows);
}

TEST(OrderTransitionCharacterizationTest, PriorityExpiryAndStaleIdsPreserveFullLedger)
{
	Ledger ledger; ledger.resetForRound(1);
	ledger.grantAllowance(Allowance::HERO, Source::PERK, 3);
	ledger.grantAllowance(Allowance::ORDER, Source::OTHER, 3);
	ledger.grantAllowance(Allowance::ORDER, Source::PERK, 2);
	ledger.grantAllowance(Allowance::ORDER, Source::ARTIFACT, 2);
	ledger.grantAllowance(Allowance::ORDER, Source::DOUBLE_COMMAND, 1);
	ledger.grantAllowance(Allowance::ORDER, Source::BATTLE_PLAN, 1);
	ledger.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 1);
	JsonNode rows;
	for(const uint32_t expected : {6u, 7u, 4u, 5u, 3u, 1u, 2u})
	{
		const auto before = ledger;
		const auto selected = ledger.eligibleAllowance(Action::ORDER, 1);
		ASSERT_TRUE(selected); EXPECT_EQ(selected->grantId, expected);
		EXPECT_FALSE(ledger.consumeAllowance(999, Action::ORDER, 1)); EXPECT_EQ(ledger, before);
		EXPECT_FALSE(ledger.consumeAllowance(expected, Action::ORDER, 2)); EXPECT_EQ(ledger, before);
		const auto paid = ledger.consumeAllowance(expected, Action::ORDER, 1); ASSERT_TRUE(paid);
		JsonNode row; row["receipt"] = receipt(*paid); row["before"] = JsonNode(wire(before));
		row["selectedExpiry"] = JsonNode(selected->expiryRound);
		row["after"] = JsonNode(wire(ledger)); rows.Vector().push_back(row);
	}
	EXPECT_FALSE(ledger.eligibleAllowance(Action::ORDER, 1));
	ASSERT_TRUE(ledger.eligibleAllowance(Action::SPELL, 1));
	EXPECT_EQ(ledger.eligibleAllowance(Action::SPELL, 1)->grantId, 8u);
	EXPECT_FALSE(ledger.eligibleAllowance(Action::SPELL, 2));
	ledger.resetForRound(2); EXPECT_EQ(ledger.remainingCounts(2), (Ledger::Counts{1, 0, 0}));
	JsonNode reset; reset["reset"] = JsonNode(wire(ledger)); rows.Vector().push_back(reset);
	publish(rows);
}

TEST(OrderTransitionCharacterizationTest, MandatePairsKeepRecipientReceiptCapAndPendingGrantRules)
{
	JsonNode rows;
	for(const uint8_t cap : {uint8_t{0}, uint8_t{1}, uint8_t{4}})
	{
		Ledger ledger; ledger.resetForRound(1);
		const auto paid = ledger.consumeAllowance(1, Action::ORDER, 1); ASSERT_TRUE(paid);
		EXPECT_TRUE(DivineMandateTransition::applyAcceptedAction(ledger, *paid, 1, false, cap, {2, 4}).empty());
		EXPECT_EQ(ledger.grants.size(), cap ? 1u : 0u);
		JsonNode row; row["cap"] = JsonNode(cap); row["primaryReceipt"] = receipt(*paid);
		row["primaryNext"] = JsonNode(wire(ledger));
		if(cap)
		{
			ASSERT_EQ(ledger.grants.front().divineMandateRecipients, (std::vector<uint32_t>{2, 4}));
			const auto pending = ledger;
			DivineMandateTransition::applyAcceptedAction(ledger, *paid, 1, false, cap, {3});
			EXPECT_EQ(ledger, pending);
			const auto second = ledger.consumeAllowance(ledger.grants.front().id, Action::SPELL, 1); ASSERT_TRUE(second);
			const auto overlap = DivineMandateTransition::applyAcceptedAction(ledger, *second, 1, true, cap, {4, 6});
			EXPECT_EQ(overlap, (std::vector<uint32_t>{4})); EXPECT_EQ(ledger.divineMandateCompletedPairs, 1);
			row["followupReceipt"] = receipt(*second); row["overlap"] = ids(overlap);
			row["pairNext"] = JsonNode(wire(ledger));
			const auto unchanged = ledger;
			const auto failure = outcome([&] { DivineMandateTransition::applyAcceptedAction(ledger, *second, 1, true, cap, {4, 4}); });
			EXPECT_EQ(failure["message"].String(), "Invalid accepted Divine Mandate recipients");
			EXPECT_EQ(ledger, unchanged); row["malformedRecipients"] = failure;
		}
		rows.Vector().push_back(row);
	}
	publish(rows);
}

TEST(OrderTransitionCharacterizationTest, InvalidFutureGrantAndNegativeQueryRejectWithoutInputChanges)
{
	Ledger ledger; ledger.resetForRound(1); const auto before = ledger;
	JsonNode rows;
	auto negative = outcome([&] { ledger.eligibleAllowance(Action::ORDER, -1); });
	EXPECT_EQ(negative["message"].String(), "Invalid Hero Action allowance query");
	EXPECT_EQ(negative["exception"].String(), "std::invalid_argument");
	EXPECT_EQ(ledger, before); negative["validInput"] = JsonNode(wire(before)); rows.Vector().push_back(negative);
	ledger.grants.front().grantedRound = 2; ledger.grants.front().expiryRound = 2;
	const auto malformed = ledger;
	auto future = outcome([&] { ledger.consumeAllowance(1, Action::ORDER, 1); });
	EXPECT_EQ(future["message"].String(), "Invalid Hero Action allowance grant ordering or lifetime");
	EXPECT_EQ(future["exception"].String(), "std::runtime_error");
	EXPECT_EQ(ledger, malformed);
	future["validBase"] = JsonNode(wire(before));
	future["onlyMutatedGrant"] = JsonNode(1); future["grantedRound"] = JsonNode(2); future["expiryRound"] = JsonNode(2);
	rows.Vector().push_back(future); publish(rows);
}

TEST(OrderTransitionCharacterizationTest, CompoundCapacityRetainsActualPartialStagingAndMandatoryGrantOrder)
{
	JsonNode rows;
	for(const uint32_t spare : {0u, 1u, 3u})
		for(const bool mandate : {false, true})
			for(const bool mandatoryDouble : {false, true})
				for(const bool morale : {false, true})
				{
					Ledger before; before.resetForRound(1);
					before.nextGrantId = std::numeric_limits<uint32_t>::max() - spare;
					const auto input = before; auto next = before;
					const auto paid = next.consumeAllowance(1, Action::ORDER, 1); ASSERT_TRUE(paid);
					std::string stage = "mandate"; bool resolved = false;
					auto result = outcome([&]
					{
						DivineMandateTransition::applyAcceptedAction(next, *paid, 1, false, mandate ? 1 : 0);
						stage = "double";
						if(mandatoryDouble) next.grantAllowance(Allowance::ORDER, Source::DOUBLE_COMMAND, 1);
						stage = "morale";
						if(morale) resolved = next.resolveHeroicSpirit(*paid, true);
					});
					const auto mandatory = static_cast<unsigned>(mandate) + static_cast<unsigned>(mandatoryDouble);
					EXPECT_EQ(result["exception"].String(), mandatory > spare ? "std::overflow_error" : "none");
					if(mandatory > spare) EXPECT_EQ(result["message"].String(), "Hero Action allowance grant ID exhausted");
					else EXPECT_EQ(resolved, morale && mandatory < spare);
					if(mandatory <= spare && mandatoryDouble)
					{
						const auto selected = next.eligibleAllowance(Action::ORDER, 1);
						ASSERT_TRUE(selected); EXPECT_EQ(selected->source, Source::DOUBLE_COMMAND);
					}
					EXPECT_EQ(before, input);
					result["spare"] = JsonNode(static_cast<int64_t>(spare)); result["mandate"] = JsonNode(mandate);
					result["double"] = JsonNode(mandatoryDouble); result["morale"] = JsonNode(morale);
					result["stage"] = JsonNode(stage); result["resolved"] = JsonNode(resolved);
					result["receipt"] = receipt(*paid); result["input"] = JsonNode(wire(before));
					result["actualStagedOutput"] = JsonNode(wire(next)); rows.Vector().push_back(result);
				}
	ASSERT_EQ(rows.Vector().size(), 24u); publish(rows);
}

TEST(OrderTransitionCharacterizationTest, ContinuationBeginValidatesBeforeInactiveAndKeepsDistinctOrderRule)
{
	const Ledger::Receipt paid{1, Action::ORDER, Allowance::HERO, Source::ROUND, 1, {}};
	JsonNode rows;
	for(const bool malformed : {false, true})
		for(const bool activePerk : {false, true})
			for(const auto primary : {HeroCommand::CHARGE, HeroCommand::SECOND_WIND})
			{
				DoubleCommandState state;
				if(malformed) state.firstOrder = HeroCommand::CHARGE;
				const auto before = state; bool began = false;
				auto row = outcome([&] { began = state.begin(paid, activePerk, primary, 1, 7,
					primary == HeroCommand::SECOND_WIND ? 9 : DoubleCommandState::INVALID_UNIT_ID); });
				EXPECT_EQ(began, activePerk && !malformed);
				EXPECT_EQ(row["exception"].String(), malformed ? "std::runtime_error" : "none");
				if(malformed)
				{
					EXPECT_EQ(row["message"].String(), "Invalid cleared Double Command state");
				}
				if(!began)
				{
					EXPECT_EQ(state, before);
				}
				row["input"] = continuation(before); row["afterBegin"] = continuation(state);
				row["activePerk"] = JsonNode(activePerk); row["malformed"] = JsonNode(malformed);
				row["primary"] = JsonNode(static_cast<int>(primary)); row["began"] = JsonNode(began);
				if(began)
				{
					const auto pending = state;
					const auto same = outcome([&] { state.completeFollowup(primary); });
					EXPECT_EQ(same["message"].String(), "Invalid Double Command follow-up Order"); EXPECT_EQ(state, pending);
					state.completeFollowup(HeroCommand::HOLD_THE_LINE);
					EXPECT_TRUE(state.used); EXPECT_EQ(state.secondWindReady(), primary == HeroCommand::SECOND_WIND);
					row["sameOrder"] = same; row["differentOrder"] = continuation(state);
					const auto target = state.consumeSecondWindContinuation();
					EXPECT_EQ(target, primary == HeroCommand::SECOND_WIND ? std::optional<uint32_t>(9) : std::nullopt);
					row["final"] = continuation(state);
				}
				rows.Vector().push_back(row);
			}
	ASSERT_EQ(rows.Vector().size(), 8u); publish(rows);
}

TEST_F(OrderTransitionAICharacterizationTest, RealAIPreparesInactiveMandateButLiveProjectionRejectsBeforePayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & live = battle()->getSide(SIDE).heroActionAllowances;
	live.grants.clear(); live.grantAllowance(Allowance::ORDER, Source::DIVINE_MANDATE, 1);
	const auto before = live; const auto rng = wire(*gameHandler->randomizer);
	OrderEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	EXPECT_FALSE(battle()->battleGetOrderActionAllowance(SIDE));
	const auto prepared = child->prepareHeroOrderAllowance(SIDE); ASSERT_TRUE(prepared);
	EXPECT_EQ(prepared->action.receipt.source, Source::DIVINE_MANDATE);
	EXPECT_TRUE(prepared->allowancesAfter.grants.empty());
	Ledger projected; projected.resetForRound(9); const auto sentinel = projected;
	HeroOrderState order;
	auto row = outcome([&] { newHorizonsHeroLuck::projectOrderAllowance(*callback, SIDE, order, nullptr, projected); });
	EXPECT_EQ(row["message"].String(), "Order has no accepted action allowance");
	EXPECT_EQ(projected, before); EXPECT_NE(projected, sentinel);
	EXPECT_EQ(live, before); EXPECT_EQ(parent->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(child->getHeroActionAllowances(SIDE), before); EXPECT_EQ(sibling->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	row["preparedReceipt"] = receipt(prepared->action.receipt);
	row["preparedBefore"] = JsonNode(wire(prepared->allowancesBefore));
	row["preparedAfter"] = JsonNode(wire(prepared->allowancesAfter));
	row["projectionSentinel"] = JsonNode(wire(sentinel)); row["failureOutput"] = JsonNode(wire(projected));
	row["rng"] = JsonNode(rng); JsonNode rows; rows.Vector().push_back(row); publish(rows);
}

TEST_F(OrderTransitionAICharacterizationTest, RealTypedNoneAcceptanceConsumesWithoutInventedMoraleAndKeepsBranchesIndependent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = battle()->getHeroActionAllowances(SIDE); const auto rng = wire(*gameHandler->randomizer);
	OrderEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto prepared = child->prepareHeroOrderAllowance(SIDE); ASSERT_TRUE(prepared);
	ASSERT_TRUE(prepared->action.typedLedger); ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	ASSERT_TRUE(child->projectAcceptedHeroOrder(SIDE, HeroCommand::NONE, {}, *prepared));
	EXPECT_EQ(child->getHeroActionAllowances(SIDE), prepared->allowancesAfter);
	EXPECT_TRUE(child->getHeroActionAllowances(SIDE).heroicSpiritProcessedReceipts.empty());
	EXPECT_FALSE(child->projectAcceptedHeroOrder(SIDE, HeroCommand::NONE, {}, *prepared));
	EXPECT_EQ(battle()->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(parent->getHeroActionAllowances(SIDE), before); EXPECT_EQ(sibling->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	JsonNode row; row["receipt"] = receipt(prepared->action.receipt);
	row["input"] = JsonNode(wire(before)); row["accepted"] = JsonNode(wire(child->getHeroActionAllowances(SIDE)));
	row["continuation"] = continuation(child->getDoubleCommandState(SIDE)); row["rng"] = JsonNode(rng);
	JsonNode rows; rows.Vector().push_back(row); publish(rows);
}

TEST_F(OrderTransitionAICharacterizationTest, RealInactivePerkAcceptsChargeWithoutStartingDoubleCommandOrMorale)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = battle()->getHeroActionAllowances(SIDE); const auto rng = wire(*gameHandler->randomizer);
	const auto continuationBefore = battle()->getDoubleCommandState(SIDE);
	OrderEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto prepared = child->prepareHeroOrderAllowance(SIDE); ASSERT_TRUE(prepared);
	ASSERT_TRUE(prepared->action.typedLedger); ASSERT_TRUE(prepared->action.isHeroAction());
	ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	ASSERT_TRUE(child->projectAcceptedHeroOrder(SIDE, HeroCommand::CHARGE, {}, *prepared));
	EXPECT_EQ(child->getHeroActionAllowances(SIDE), prepared->allowancesAfter);
	EXPECT_TRUE(child->getHeroActionAllowances(SIDE).heroicSpiritProcessedReceipts.empty());
	EXPECT_EQ(child->getDoubleCommandState(SIDE), continuationBefore);
	EXPECT_FALSE(child->getDoubleCommandState(SIDE).used);
	EXPECT_FALSE(child->battleHasPendingDoubleCommand(SIDE));
	EXPECT_EQ(parent->getHeroActionAllowances(SIDE), before); EXPECT_EQ(sibling->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(battle()->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(parent->getDoubleCommandState(SIDE), continuationBefore);
	EXPECT_EQ(sibling->getDoubleCommandState(SIDE), continuationBefore);
	EXPECT_EQ(battle()->getDoubleCommandState(SIDE), continuationBefore);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	JsonNode row; row["receipt"] = receipt(prepared->action.receipt);
	row["input"] = JsonNode(wire(before)); row["accepted"] = JsonNode(wire(child->getHeroActionAllowances(SIDE)));
	row["continuation"] = continuation(child->getDoubleCommandState(SIDE)); row["rng"] = JsonNode(rng);
	JsonNode rows; rows.Vector().push_back(row); publish(rows);
}

TEST_F(OrderTransitionAICharacterizationTest, RealLegacySyntheticReceiptAndNonePathStayUntypedWithoutMorale)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->getSide(SIDE).heroActionAllowances = Ledger{};
	const auto before = battle()->getHeroActionAllowances(SIDE); const auto rng = wire(*gameHandler->randomizer);
	OrderEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto prepared = child->prepareHeroOrderAllowance(SIDE); ASSERT_TRUE(prepared);
	EXPECT_FALSE(prepared->action.typedLedger);
	EXPECT_EQ(prepared->action.receipt, (Ledger::Receipt{0, Action::ORDER, Allowance::HERO, Source::OTHER, 1, {}}));
	EXPECT_EQ(prepared->allowancesBefore, before); EXPECT_EQ(prepared->allowancesAfter, before);
	ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	ASSERT_TRUE(child->projectAcceptedHeroOrder(SIDE, HeroCommand::NONE, {}, *prepared));
	EXPECT_EQ(child->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(parent->getHeroActionAllowances(SIDE), before); EXPECT_EQ(sibling->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(battle()->getHeroActionAllowances(SIDE), before); EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	JsonNode row; row["receipt"] = receipt(prepared->action.receipt); row["typed"] = JsonNode(prepared->action.typedLedger);
	row["input"] = JsonNode(wire(before)); row["accepted"] = JsonNode(wire(child->getHeroActionAllowances(SIDE)));
	row["continuation"] = continuation(child->getDoubleCommandState(SIDE)); row["rng"] = JsonNode(rng);
	JsonNode rows; rows.Vector().push_back(row); publish(rows);
}

TEST_F(OrderTransitionAICharacterizationTest, RealInactivePerkStillValidatesMalformedBeginAfterTypedPayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & side = battle()->getSide(SIDE);
	side.doubleCommandState.firstOrder = HeroCommand::CHARGE;
	const auto malformed = side.doubleCommandState;
	const auto before = side.heroActionAllowances; const auto rng = wire(*gameHandler->randomizer);
	OrderEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto prepared = child->prepareHeroOrderAllowance(SIDE); ASSERT_TRUE(prepared);
	ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	auto row = outcome([&] { child->projectAcceptedHeroOrder(SIDE, HeroCommand::CHARGE, {}, *prepared); });
	EXPECT_EQ(row["message"].String(), "Invalid cleared Double Command state");
	// This legacy late failure is deliberately NOT modeled as an atomic operation.
	EXPECT_EQ(child->getHeroActionAllowances(SIDE), prepared->allowancesAfter);
	EXPECT_NE(child->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(child->getDoubleCommandState(SIDE), malformed);
	EXPECT_EQ(parent->getHeroActionAllowances(SIDE), before); EXPECT_EQ(sibling->getHeroActionAllowances(SIDE), before);
	EXPECT_EQ(side.heroActionAllowances, before); EXPECT_EQ(side.doubleCommandState, malformed);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	row["input"] = JsonNode(wire(before)); row["lateFailureOutput"] = JsonNode(wire(child->getHeroActionAllowances(SIDE)));
	row["continuation"] = continuation(child->getDoubleCommandState(SIDE)); row["rng"] = JsonNode(rng);
	JsonNode rows; rows.Vector().push_back(row); publish(rows);
}

TEST_F(OrderTransitionAICharacterizationTest, RealDoubleCommandPrimaryFollowupExhaustionAndLateGrantFailureStayDistinct)
{
	startGame();
	const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:command"));
	ASSERT_TRUE(skill.hasValue());
	const std::array<const char *, 3> perks{"new-horizons:command.aggressiveCommander",
		"new-horizons:command.veteranCommander", "new-horizons:command.doubleCommand"};
	for(size_t index = 0; index < perks.size(); ++index)
	{
		attackerSideHero->setSecSkillLevel(skill, static_cast<MasteryLevel::Type>(index + 1), ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:command", perks[index]});
	}
	ASSERT_TRUE(heroCommands::hasDoubleCommand(attackerSideHero));
	startBattle();
	BattleUnitsChanged removed; removed.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(removed);
	auto * active = addStack(SIDE, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * ward = addStack(SIDE, creatureByName("core:pikeman"), BattleHex(71), 100);
	auto * shooter = addStack(SIDE, creatureByName("core:archer"), BattleHex(3, 5), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 5), 100);
	ASSERT_NE(active, nullptr); ASSERT_NE(ward, nullptr); ASSERT_NE(shooter, nullptr); ASSERT_NE(enemy, nullptr);
	beginCombat();
	BattleSetActiveStack activated; activated.battleID = BattleID(0); activated.stack = active->unitId();
	activated.reason = BattleUnitTurnReason::TURN_QUEUE; gameHandler->sendAndApply(activated);
	shooter->movedThisRound = true;
	const auto mandateSkill = SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate"));
	ASSERT_TRUE(mandateSkill.hasValue());
	const auto mandateRankBefore = attackerSideHero->getSecSkillLevel(mandateSkill);
	ASSERT_EQ(mandateRankBefore, 0);
	ASSERT_FALSE(battle()->battleGetDivineMandateStatus(SIDE).active);
	JsonNode rows;
	for(const int mode : {0, 1, 2})
	{
		auto & live = battle()->getSide(SIDE).heroActionAllowances;
		if(mode == 2)
		{
			// Real accepted dedicated Orders cannot trigger Double Command. Leave only Second Wind unused.
			for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::FOCUS_FIRE,
				HeroCommand::RIPOSTE, HeroCommand::BRACE, HeroCommand::PROTECT, HeroCommand::FLANK})
			{
				live.grantAllowance(Allowance::ORDER, Source::PERK, 1);
				auto action = BattleAction::makeHeroCommand(SIDE, command);
				if(command == HeroCommand::FOCUS_FIRE || command == HeroCommand::FLANK)
					action = BattleAction::makeTargetedHeroCommand(SIDE, command, enemy->unitId());
				if(command == HeroCommand::PROTECT)
					action = BattleAction::makePairedHeroCommand(SIDE, command, active->unitId(), ward->unitId());
				ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
			}
			ASSERT_FALSE(battle()->getDoubleCommandState(SIDE).used);
		}
		const auto savedNextId = live.nextGrantId;
		if(mode == 1)
		{
			ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
			ASSERT_TRUE(attackerSideHero->usesPrimaryGrowth());
			attackerSideHero->setSecSkillLevel(mandateSkill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			ASSERT_EQ(attackerSideHero->getSecSkillLevel(mandateSkill), MasteryLevel::BASIC);
			ASSERT_TRUE(battle()->battleGetDivineMandateStatus(SIDE).active);
			ASSERT_EQ(battle()->battleGetDivineMandateStatus(SIDE).maximumPairs, 1);
			// Preparation reserves one required slot, but Mandate consumes it before Double Command.
			live.nextGrantId = std::numeric_limits<uint32_t>::max() - 1;
		}
		const auto before = live; const auto beforeContinuation = battle()->getDoubleCommandState(SIDE);
		const auto rng = wire(*gameHandler->randomizer);
		OrderEnvironment environment(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
		auto child = std::make_shared<HypotheticBattle>(&environment, parent);
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		const auto prepared = child->prepareHeroOrderAllowance(SIDE); ASSERT_TRUE(prepared);
		ASSERT_EQ(prepared->action.receipt.allowance, Allowance::HERO);
		ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
		const auto command = mode == 2 ? HeroCommand::SECOND_WIND : HeroCommand::CHARGE;
		const std::vector<uint32_t> targets = mode == 2 ? std::vector<uint32_t>{shooter->unitId()} : std::vector<uint32_t>{};
		ASSERT_TRUE(child->battlePrepareHeroOrderState(SIDE, command, targets));
		bool accepted = false;
		auto row = outcome([&] { accepted = child->projectAcceptedHeroOrder(SIDE, command, targets, *prepared); });
		EXPECT_EQ(accepted, mode != 1);
		EXPECT_EQ(row["exception"].String(), mode == 1 ? "std::overflow_error" : "none");
		if(mode == 1)
		{
			EXPECT_EQ(row["message"].String(), "Hero Action allowance grant ID exhausted");
		}
		const auto primary = child->getDoubleCommandState(SIDE);
		EXPECT_TRUE(primary.used); EXPECT_EQ(primary.orderPending(), mode != 2);
		EXPECT_EQ(primary.firstOrder, command);
		EXPECT_TRUE(child->getHeroActionAllowances(SIDE).heroicSpiritProcessedReceipts.empty());
		row["mode"] = JsonNode(mode); row["primaryReceipt"] = receipt(prepared->action.receipt);
		row["input"] = JsonNode(wire(before)); row["afterPrimary"] = JsonNode(wire(child->getHeroActionAllowances(SIDE)));
		row["afterBegin"] = continuation(primary);
		if(mode == 0)
		{
			const auto followup = child->prepareHeroOrderAllowance(SIDE); ASSERT_TRUE(followup);
			ASSERT_EQ(followup->action.receipt.source, Source::DOUBLE_COMMAND);
			ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *followup));
			ASSERT_TRUE(child->projectAcceptedHeroOrder(SIDE, HeroCommand::HOLD_THE_LINE, {}, *followup));
			EXPECT_TRUE(child->getDoubleCommandState(SIDE).used);
			EXPECT_FALSE(child->getDoubleCommandState(SIDE).orderPending());
			EXPECT_EQ(child->getHeroActionAllowances(SIDE), followup->allowancesAfter);
			row["followupReceipt"] = receipt(followup->action.receipt);
		}
		else
		{
			auto expected = prepared->allowancesAfter;
			if(mode == 1)
			{
				const auto mandateId = expected.grantAllowance(Allowance::SPELL, Source::DIVINE_MANDATE, 1);
				EXPECT_EQ(mandateId, std::numeric_limits<uint32_t>::max() - 1);
				row["firstMandateAllocation"] = JsonNode(static_cast<int64_t>(mandateId));
			}
			else
			{
				const auto discardedId = expected.grantAllowance(Allowance::ORDER, Source::DOUBLE_COMMAND, 1);
				const auto removed = std::erase_if(expected.grants, [discardedId](const Ledger::Grant & grant)
				{
					return grant.id == discardedId;
				});
				EXPECT_EQ(removed, 1u);
				EXPECT_EQ(expected.nextGrantId, discardedId + 1);
				row["discardedDoubleCommandAllocation"] = JsonNode(static_cast<int64_t>(discardedId));
			}
			EXPECT_EQ(child->getHeroActionAllowances(SIDE), expected);
		}
		EXPECT_TRUE(child->getHeroActionAllowances(SIDE).heroicSpiritProcessedReceipts.empty());
		row["finalLedger"] = JsonNode(wire(child->getHeroActionAllowances(SIDE)));
		row["finalContinuation"] = continuation(child->getDoubleCommandState(SIDE)); row["rng"] = JsonNode(rng);
		EXPECT_EQ(parent->getHeroActionAllowances(SIDE), before); EXPECT_EQ(sibling->getHeroActionAllowances(SIDE), before);
		EXPECT_EQ(parent->getDoubleCommandState(SIDE), beforeContinuation);
		EXPECT_EQ(sibling->getDoubleCommandState(SIDE), beforeContinuation);
		EXPECT_EQ(live, before); EXPECT_EQ(battle()->getDoubleCommandState(SIDE), beforeContinuation);
		EXPECT_EQ(wire(*gameHandler->randomizer), rng);
		rows.Vector().push_back(row); live.nextGrantId = savedNextId;
		if(mode == 1)
		{
			attackerSideHero->setSecSkillLevel(mandateSkill, mandateRankBefore, ChangeValueMode::ABSOLUTE);
			ASSERT_EQ(attackerSideHero->getSecSkillLevel(mandateSkill), mandateRankBefore);
			ASSERT_FALSE(battle()->battleGetDivineMandateStatus(SIDE).active);
		}
	}
	ASSERT_EQ(rows.Vector().size(), 3u); publish(rows);
}
