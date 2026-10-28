/*
 * HeroOrderDecisionMeasurementTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "HeroActionTimingCapture.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/HeroOrderCandidateRules.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/NewHorizonsHeroLuck.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include <boost/algorithm/hex.hpp>
#include <array>
#include <set>
#include <bit>
#include <chrono>
#include <locale>
#include <sstream>

namespace heroQueueTest
{
namespace
{
struct Trial { int pair; bool revised; };
// Existing bounded log-sink ownership. Type rows distinguish the revised Orders
// from the six retained candidates evaluated in the same real selector call.
class OrderTypeTarget final : public ILogTarget
{
	std::weak_ptr<heroTimingTest::Messages> messages;
public:
	explicit OrderTypeTarget(std::weak_ptr<heroTimingTest::Messages> value) : messages(value) {}
	void write(const LogRecord & record) override
	{
		if(!record.message.starts_with("NH_HERO_ACTION_TYPE_TIMING ")) return;
		if(const auto output = messages.lock())
		{
			std::lock_guard lock(output->mutex);
			if(output->rows.size() < 32) output->rows.push_back(record.message);
			else output->truncated = true;
		}
	}
};

void expectNoRevisedSearch(const std::shared_ptr<heroTimingTest::Messages> & messages)
{
	std::lock_guard lock(messages->mutex);
	ASSERT_FALSE(messages->truncated);
	std::set<int> checked;
	for(const auto & row : messages->rows)
	{
		std::map<std::string, int64_t> fields;
		std::istringstream input(row);
		std::string word;
		while(input >> word)
		{
			const auto equal = word.find('=');
			if(equal != std::string::npos) fields.emplace(word.substr(0, equal), std::stoll(word.substr(equal + 1)));
		}
		const int command = fields.at("command");
		if(command != static_cast<int>(HeroCommand::FEINT) && command != static_cast<int>(HeroCommand::VANGUARD)) continue;
		checked.insert(command);
		EXPECT_EQ(fields.at("evaluations"), 1);
		for(const auto * stage : {"full_calls", "cached_calls", "movement_sum_us", "best_target_sum_us"})
			EXPECT_EQ(fields.at(stage), 0) << row;
	}
	EXPECT_EQ(checked, (std::set<int>{static_cast<int>(HeroCommand::FEINT), static_cast<int>(HeroCommand::VANGUARD)}));
}

class Counts final : public heroActionSelection::Observer
{
public:
	static constexpr size_t capacity = 256;
	std::array<size_t, 15> byCommand{};
	std::array<heroActionSelection::CandidateValue, capacity> values{};
	heroActionSelection::SelectionValue selection{};
	size_t candidates = 0, choices = 0;
	bool spells = false, invalid = false;
	void candidate(heroActionSelection::CandidateValue value) noexcept override
	{
		if(candidates < values.size()) values[candidates] = value;
		else invalid = true;
		invalid |= value.ordinal != candidates++;
		spells |= value.spell != SpellID::NONE;
		const auto index = static_cast<size_t>(value.command);
		if(index < byCommand.size()) ++byCommand[index];
		else invalid = true;
	}
	void selected(heroActionSelection::SelectionValue value) noexcept override
	{
		invalid |= choices != 0 || value.ordinal >= candidates;
		selection = value;
		++choices;
	}
	size_t count(HeroCommand command) const { return byCommand.at(static_cast<size_t>(command)); }
	std::string trace() const
	{
		// Formatting is outside timing. This scalar roster deliberately excludes
		// optional detailed identities; complete submitted action wires follow.
		std::ostringstream output;
		output.imbue(std::locale::classic());
		output << "v1;scope=ordered-scalar-candidates;capacity=" << capacity
			<< ";count=" << candidates << ";choices=" << choices << ";invalid=" << invalid;
		for(size_t index = 0; index < std::min(candidates, values.size()); ++index)
		{
			const auto & value = values[index];
			output << ";C=" << value.ordinal << ',' << static_cast<int>(value.command)
				<< ',' << value.spell.getNum() << ',' << value.overcharge
				<< ',' << std::bit_cast<uint32_t>(value.value);
		}
		if(choices)
			output << ";S=" << selection.ordinal << ',' << std::bit_cast<uint32_t>(selection.baseline)
				<< ',' << static_cast<int>(selection.disposition);
		return output.str();
	}
};

std::string asciiHex(const std::string & value)
{
	std::string result;
	boost::algorithm::hex(value, std::back_inserter(result));
	return result;
}
}

class HeroOrderDecisionMeasurementTest : public HeroQueueSelectorTest,
	public ::testing::WithParamInterface<Trial>
{
protected:
	void SetUp() override
	{
		HeroQueueSelectorTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}
	void prepareHeroes()
	{
		useRevisedOrders = GetParam().revised; // Capture before creating either world or battle.
		dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(seed);
		ASSERT_NO_FATAL_FAILURE(startGame());
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			hero->removeAllSpells();
			if(hero->hasSpellbook()) hero->removeArtifact(ArtifactPosition::SPELLBOOK);
			hero->setPrimarySkill(PrimarySkill::ATTACK, 150, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::DEFENSE, 150, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 0, ChangeValueMode::ABSOLUTE);
			const auto leadership = hero->getLeadershipCapacity();
			ASSERT_TRUE(leadership);
			ASSERT_LE(leadership->capacity, 2250);
			hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::LEADERSHIP, BonusSource::OTHER, static_cast<int>(2250 - leadership->capacity), BonusSourceID()));
			ASSERT_EQ(hero->getLeadershipCapacity()->capacity, 2250);
			ASSERT_FALSE(hero->hasSpellbook());
			ASSERT_TRUE(hero->getSpellsInSpellbook().empty());
			ASSERT_FALSE(newHorizonsHeroLuck::hasLuckyDay(hero));
		}
	}
	void prepareArena()
	{
		ASSERT_NO_FATAL_FAILURE(startBattle());
		BattleUnitsChanged clear;
		clear.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			clear.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(clear);
		for(int index = 0; index < 7; ++index)
		{
			const bool shooter = index == 6;
			auto * own = addStack(BattleSide::ATTACKER,
				creatureByName(shooter ? "core:archer" : "core:swordsman"),
				shooter ? BattleHex(5, 8) : BattleHex(7, index + 2), 15);
			ASSERT_NE(own, nullptr);
			if(index == 0) active = own;
			ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:swordsman"),
				BattleHex(8, index + 2), 15), nullptr);
		}
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NO_FATAL_FAILURE(activate(active));
		callback = std::make_shared<QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		for(const auto * unit : live->battleGetAllUnits(false)) (void)unit->ableToRetaliate();
		environment = std::make_shared<QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		ASSERT_EQ(live->battleGetAllUnits(true).size(), 14u);
		ASSERT_TRUE(live->isMeleeAttackPossible(active, live->battleGetUnitByPos(BattleHex(8, 2))));
	}
	std::string comparableInputs()
	{
		// Deliberately exclude only captured command definitions/version. Unit
		// state includes bonuses, positions, counts, activations and allowances.
		JsonNode result;
		result["seed"].Integer() = seed;
		result["unitsAndActionState"].String() = state(*live);
		result["authorityRng"].String() = wire(*gameHandler->randomizer);
		result["fixtureRng"].String() = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			auto & row = result[hero == attackerSideHero ? "attacker" : "defender"];
			row["heroType"].Integer() = hero->getHeroTypeID().getNum();
			row["level"].Integer() = hero->level;
			row["leadership"].Integer() = hero->getLeadershipCapacity()->capacity;
			row["spellbook"].Bool() = hero->hasSpellbook();
			row["spells"].String() = wire(hero->getSpellsInSpellbook());
			row["perkState"].String() = wire(hero->getPerkState());
			for(auto attribute : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE,
				PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
				row["primary"].Vector().emplace_back(hero->getPrimSkillLevel(attribute));
		}
		return result.toString();
	}
};

TEST_P(HeroOrderDecisionMeasurementTest, MeasuresOneExistingProductionOrderDecision)
{
	ASSERT_NO_FATAL_FAILURE(prepareHeroes());
	ASSERT_NO_FATAL_FAILURE(prepareArena());
	RecordProperty("pair", std::to_string(GetParam().pair));
	RecordProperty("profile", GetParam().revised ? "V4" : "V3");
	// Serialized state contains binary bytes. Preserve exact paired identity in
	// ASCII rather than letting the XML writer discard invalid text characters.
	std::string comparable;
	boost::algorithm::hex(comparableInputs(), std::back_inserter(comparable));
	RecordProperty("comparableInputs", comparable);
	const auto before = observation();
	const auto capturedRules = wire(battle()->getHeroCommandRules());
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::ATTACKER);
	BattleEvaluator evaluator(environment, callback, child, cache, active, PlayerColor(0),
		BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active); // Ordinary baseline setup is outside timing.
	ASSERT_TRUE(evaluator.canCastSpell());
	heroTimingTest::Capture timingCapture;
	auto orderTypes = std::make_shared<heroTimingTest::Messages>();
	CLogger::getGlobalLogger()->addTarget(std::make_unique<OrderTypeTarget>(orderTypes));
	Counts counts;
	std::unique_ptr<heroTimingTest::Capture> capture;
	if(GetParam().pair == 0 && GetParam().revised && heroTimingTest::optedIn())
		capture = std::make_unique<heroTimingTest::Capture>();
	const auto started = std::chrono::steady_clock::now();
	const bool submitted = evaluator.attemptCastingSpell(active, true, &counts);
	const auto elapsed = std::chrono::steady_clock::now() - started;
	const auto after = observation();
	if(GetParam().revised && heroTimingTest::optedIn()) expectNoRevisedSearch(orderTypes);
	RecordProperty("selectionTrace", counts.trace());
	RecordProperty("observationBeforeHex", asciiHex(before));
	RecordProperty("observationAfterHex", asciiHex(after));
	RecordProperty("submitted", submitted ? "1" : "0");
	// wire() already emits hex for the complete native BattleAction serializer,
	// including every target and option, rather than a chosen-command summary.
	RecordProperty("heroRequestWires", wire(callback->heroRequests));
	RecordProperty("unitRequestWires", wire(callback->unitRequests));
	RecordProperty("decisionNanoseconds", std::to_string(
		std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
	if(capture)
	{
		capture->expectSubmission(battle()->getRound(), active->unitId(), true);
		const auto rows = capture->rows();
		ASSERT_EQ(rows.size(), 1u);
		RecordProperty("heroActionTimingAggregate", rows.front());
	}
	RecordProperty("candidateEvaluations", std::to_string(counts.candidates));
	RecordProperty("choices", std::to_string(counts.choices));
	for(auto command : {HeroCommand::PROTECT, HeroCommand::FLANK, HeroCommand::FEINT, HeroCommand::VANGUARD})
		RecordProperty("candidates_" + std::string(heroCommands::key(command)), std::to_string(counts.count(command)));
	EXPECT_EQ(wire(battle()->getHeroCommandRules()), capturedRules);
	EXPECT_EQ(after, before); // Native source, RNG, untouched parent and sibling.
	EXPECT_FALSE(counts.invalid);
	EXPECT_FALSE(counts.spells);
	EXPECT_GT(counts.candidates, 0u);
	EXPECT_EQ(counts.choices, 1u);
	EXPECT_TRUE(callback->unitRequests.empty());
	ASSERT_EQ(callback->heroRequests.size(), submitted ? 1u : 0u);
	if(submitted)
	{
		const auto & action = callback->heroRequests.front();
		EXPECT_EQ(action.actionType, EActionType::HERO_COMMAND);
		EXPECT_EQ(action.side, BattleSide::ATTACKER);
		std::vector<uint32_t> targets;
		for(const auto & destination : action.target)
		{
			ASSERT_GE(destination.unitValue, 0);
			EXPECT_EQ(destination.hexValue, BattleHex::INVALID);
			targets.push_back(static_cast<uint32_t>(destination.unitValue));
		}
		EXPECT_TRUE(heroOrderCandidates::commandTargetIsLegal(*live, action.side, action.command, targets));
		RecordProperty("chosenCommand", std::string(heroCommands::key(action.command)));
	}
	else RecordProperty("chosenCommand", "none");
	if(GetParam().revised)
	{
		EXPECT_EQ(counts.count(HeroCommand::PROTECT) + counts.count(HeroCommand::FLANK), 0u);
		EXPECT_EQ(counts.count(HeroCommand::FEINT), 1u);
		EXPECT_EQ(counts.count(HeroCommand::VANGUARD), 1u);
	}
	else
	{
		EXPECT_GT(counts.count(HeroCommand::PROTECT), 0u);
		EXPECT_GT(counts.count(HeroCommand::FLANK), 0u);
		EXPECT_EQ(counts.count(HeroCommand::FEINT) + counts.count(HeroCommand::VANGUARD), 0u);
	}
}

INSTANTIATE_TEST_SUITE_P(BoundedPairs, HeroOrderDecisionMeasurementTest,
	::testing::Values(Trial{0, false}, Trial{0, true}, Trial{1, true},
		Trial{1, false}, Trial{2, false}, Trial{2, true}),
	[](const ::testing::TestParamInfo<Trial> & trial)
	{
		return "Pair" + std::to_string(trial.param.pair) + (trial.param.revised ? "V4" : "V3");
	});
}
