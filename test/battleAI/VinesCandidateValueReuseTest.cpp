/*
 * VinesCandidateValueReuseTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionSelectionState.h"
#include "HeroActionTimingCapture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleEvaluatorSpellProjection.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/VinesCandidateValueReuse.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/spells/NewHorizonsSpellcraft.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <sstream>
#include <tuple>

namespace
{
SpellID vinesId()
{
	return SpellID(SpellID::decode(std::string(newHorizonsVengefulVines::SPELL_KEY)));
}

PossibleSpellcast candidate()
{
	PossibleSpellcast cast;
	cast.spell = vinesId().toSpell();
	cast.dest = {spells::Destination(BattleHex(8, 5)), spells::Destination(BattleHex(9, 5)),
		spells::Destination(BattleHex(10, 5))};
	cast.value = -0.0f;
	return cast;
}

heroActionProjection::Admission admission()
{
	heroActionProjection::Admission result;
	result.admitted = true;
	result.spellAllowance.emplace();
	result.spellAllowance->spell = vinesId();
	result.counterspell.resolutionKnown = true;
	result.counterspell.negated = false;
	return result;
}

newHorizonsVengefulVines::ProjectionReceipt receipt()
{
	// Synthetic receipt tests the value container only, not mechanics attestation.
	newHorizonsVengefulVines::ProjectionReceipt result;
	result.effects.push_back({"directDamage", spells::effects::ForecastImplementation::CANONICAL_DAMAGE,
		{{10, BattleHex(8, 5)}, {11, BattleHex(10, 5)}}});
	result.effects.push_back({"slowingVines", spells::effects::ForecastImplementation::CANONICAL_TIMED,
		{{10, BattleHex(8, 5)}, {11, BattleHex(10, 5)}}});
	return result;
}

class Callback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	Callback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::string byteHex(const std::vector<std::byte> & bytes)
{
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(bytes.size() * 2);
	for(const auto byte : bytes)
	{
		const auto value = std::to_integer<unsigned int>(byte);
		result.push_back(digits[value >> 4]);
		result.push_back(digits[value & 15]);
	}
	return result;
}

class Scores final : public heroActionSelection::Observer
{
public:
	using Row = std::tuple<size_t, HeroCommand, SpellID, int32_t, uint32_t>;
	std::vector<Row> rows;
	std::optional<heroActionSelection::SelectionValue> selection;
	void candidate(heroActionSelection::CandidateValue value) noexcept override
	{
		rows.emplace_back(value.ordinal, value.command, value.spell, value.overcharge,
			std::bit_cast<uint32_t>(value.value));
	}
	void selected(heroActionSelection::SelectionValue value) noexcept override { selection = value; }
};

void appendSpellReceipts(std::vector<std::byte> & bytes, const CBattleInfoCallback & view)
{
	const auto append = [&](const auto & value)
	{
		const auto more = projectedHitTest::serialized(value);
		bytes.insert(bytes.end(), more.begin(), more.end());
	};
	const auto * state = view.getBattle();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		append(state->getExtendSpellLastRound(side));
		append(state->hasCompletedHeroSpellCast(side));
		for(int32_t level = 1; level <= 5; ++level)
			append(state->hasCompletedHeroSpellLevel(side, level));
		append(state->getCastSpells(side));
		append(state->getCrossSchoolFormulaState(side));
		append(state->getSpellResponseState(side));
		append(state->getOverwhelmingFormulaState(side));
		append(state->getMetamagicPendingCount(side));
		append(state->getMetamagicUsesConsumed(side));
		append(state->getMetamagicGrandUsed(side));
		append(state->getMetamagicFormulaReserveUsed(side));
		append(state->getMetamagicCountersequenceArmed(side));
		append(state->getMetamagicFirstSpell(side));
		append(state->getMetamagicFirstTargetUnitId(side));
		append(state->getMetamagicSequenceSpells(side));
		append(state->getMetamagicFirstCounterspellNegated(side));
	}
}
}

TEST(VinesCandidateValueReuseTest, ExactOrderedReceiptAndFullOptionsOnlyWithinOneFrame)
{
	auto cast = candidate();
	ASSERT_NE(cast.spell, nullptr);
	auto prepared = admission();
	auto proof = receipt();
	VinesCandidateValueReuse values;
	values.remember(cast, prepared, {}, proof);
	// Different original geometry, same certified ordered effects: copy exact -0.
	cast.dest.back() = spells::Destination(BattleHex(9, 4));
	const auto hit = values.find(cast, prepared, {}, proof);
	ASSERT_TRUE(hit);
	EXPECT_EQ(std::bit_cast<uint32_t>(*hit), std::bit_cast<uint32_t>(-0.0f));
	std::swap(proof.effects.front().destinations[0], proof.effects.front().destinations[1]);
	EXPECT_FALSE(values.find(cast, prepared, {}, proof));
	proof = receipt();
	++proof.concentrationBonusPercent;
	EXPECT_FALSE(values.find(cast, prepared, {}, proof));
	proof = receipt();
	++prepared.spellAllowance->action.epoch;
	EXPECT_FALSE(values.find(cast, prepared, {}, proof));
	prepared = admission();
	cast.metamagicGrand = true;
	EXPECT_FALSE(values.find(cast, prepared, {}, proof));
	cast.metamagicGrand = false;
	EXPECT_FALSE(values.find(cast, prepared, {10}, proof));
	VinesCandidateValueReuse nextInvocation;
	EXPECT_FALSE(nextInvocation.find(cast, prepared, {}, proof));
	cast.command = HeroCommand::CHARGE;
	values.beginCandidate(cast); // Unsupported work may mutate a shared input cache.
	cast.command = HeroCommand::NONE;
	EXPECT_FALSE(values.find(cast, prepared, {}, proof));
}

TEST(VinesCandidateValueReuseTest, MissingProofDisabledNegatedAndInvalidValuesFailClosed)
{
	auto cast = candidate();
	ASSERT_NE(cast.spell, nullptr);
	auto prepared = admission();
	const auto proof = receipt();
	VinesCandidateValueReuse values;
	for(const float invalid : {std::numeric_limits<float>::lowest(),
		static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE),
		std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		cast.value = invalid;
		values.remember(cast, prepared, {}, proof);
		EXPECT_FALSE(values.find(cast, prepared, {}, proof));
	}
	cast.value = 1.25f;
	values.remember(cast, prepared, {}, proof);
	EXPECT_FALSE(values.find(cast, prepared, {}, std::nullopt));
	EXPECT_FALSE(values.find(cast, prepared, {}, proof)); // Missing proof clears prior entries.
	prepared.counterspellNegated = true;
	values.remember(cast, prepared, {}, proof);
	EXPECT_FALSE(values.find(cast, prepared, {}, proof));
	VinesCandidateValueReuse::TestControl control{false};
	VinesCandidateValueReuse disabled(&control);
	prepared = admission();
	disabled.remember(cast, prepared, {}, proof);
	EXPECT_FALSE(disabled.find(cast, prepared, {}, proof));
	EXPECT_EQ(control.hits, 0u);
}

class VinesCandidateValueReuseAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	std::shared_ptr<Callback> callback;
	std::shared_ptr<projectedHitTest::Environment> environment;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(bool withPerks)
	{
		dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(seed);
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(vinesId());
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW); // Remains a legal competitor.
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 30, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		if(withPerks)
		{
			const std::string skill(newHorizonsMagic::SPELLCRAFT_SKILL);
			const int id = SecondarySkill::decode(skill);
			ASSERT_GE(id, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(id), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({skill, std::string(newHorizonsSpellcraft::CONCENTRATION)});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, std::string(newHorizonsSpellcraft::CONCENTRATION)));
			attackerSideHero->setSecSkillLevel(SecondarySkill(id), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({skill, std::string(newHorizonsSpellcraft::EXTEND_SPELL)});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, std::string(newHorizonsSpellcraft::EXTEND_SPELL)));
		}
		startBattle();
		beginCombat();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removed);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 5), 30);
		ASSERT_NE(active, nullptr);
		if(withPerks)
		{
			ASSERT_NO_FATAL_FAILURE(prepareEventStatuses());
		}
		for(const auto position : {BattleHex(10, 5), BattleHex(11, 5)})
		{
			auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), position, 100);
			ASSERT_NE(enemy, nullptr);
			if(withPerks)
				enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
					BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER, -1, BonusSourceID()));
		}
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<Callback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<projectedHitTest::Environment>(gameState());
		// Ordinary getter warm-up only, before the full-byte baseline (latch retained).
		for(const auto * unit : battle()->battleGetAllStacks(false))
			(void)unit->counterAttacks.total();
	}
	void prepareCurrentMarkedRecipients()
	{
		ASSERT_TRUE(newHorizonsMetamagic::usesArcaneMarks(attackerSideHero->getPerkState().rules));
		const auto skill = "new-horizons:metamagic";
		const int id = SecondarySkill::decode(skill);
		ASSERT_GE(id, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(id), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		for(const auto * perk : {"new-horizons:metamagic.lingeringMagic",
			"new-horizons:metamagic.arcaneSaturation", "new-horizons:metamagic.arcanePerfection"})
		{
			attackerSideHero->applyPerkSelection({skill, perk});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
		}
		const auto profile = newHorizonsMetamagic::profile(attackerSideHero);
		ASSERT_EQ(profile.rank, 3);
		ASSERT_TRUE(profile.lingeringMagic);
		const auto recipients = battle()->battleAliveUnits(BattleSide::DEFENDER);
		ASSERT_EQ(recipients.size(), 2u);
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		for(size_t index = 0; index < recipients.size(); ++index)
		{
			auto state = recipients[index]->acquireState();
			state->arcaneMarks.set(BattleSide::ATTACKER, static_cast<int32_t>(index * 5));
			state->arcaneMarks.set(BattleSide::DEFENDER, static_cast<int32_t>((1 - index) * 5));
			packet.changedStacks.emplace_back(recipients[index]->unitId(), UnitChanges::EOperation::UPDATE);
			packet.changedStacks.back().data = state->save();
		}
		gameHandler->sendAndApply(packet);
	}
	void checkCurrentMarkedProjection()
	{
		const auto recipients = battle()->battleAliveUnits(BattleSide::DEFENDER);
		ASSERT_EQ(recipients.size(), 2u);
		const auto before = decisionSnapshot();
		auto state = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		auto cast = candidate();
		cast.dest.clear();
		const auto & triples = newHorizonsVengefulVines::connectedTriples();
		const auto found = std::find_if(triples.begin(), triples.end(), [&](const auto & hexes)
		{
			return hexes.contains(recipients[0]->getPosition()) && hexes.contains(recipients[1]->getPosition());
		});
		ASSERT_NE(found, triples.end());
		for(const auto hex : *found) cast.dest.emplace_back(hex);
		const auto admission = heroActionProjection::admitCandidate(*state, BattleSide::ATTACKER, cast);
		ASSERT_TRUE(admission.admitted);
		uint32_t targetId = std::numeric_limits<uint32_t>::max();
		const auto projected = battleEvaluatorProjection::projectSpell(*state, *callback->getBattle(BattleID(0)),
			attackerSideHero, BattleSide::ATTACKER, cast, admission, targetId, {});
		ASSERT_TRUE(projected.accepted);
		ASSERT_TRUE(projected.vines);
		const auto * low = state->battleGetUnitByID(recipients[0]->unitId());
		const auto * high = state->battleGetUnitByID(recipients[1]->unitId());
		ASSERT_NE(low, nullptr);
		ASSERT_NE(high, nullptr);
		EXPECT_GT(recipients[1]->getAvailableHealth() - high->getAvailableHealth(),
			recipients[0]->getAvailableHealth() - low->getAvailableHealth());
		const auto selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(vinesId()))
			.And(Selector::type()(BonusType::STACKS_MOVEMENT_RANGE));
		const auto lowBonuses = low->getAllBonuses(selector);
		const auto highBonuses = high->getAllBonuses(selector);
		ASSERT_EQ(lowBonuses->size(), 1u);
		ASSERT_EQ(highBonuses->size(), 1u);
		EXPECT_LT((*highBonuses)[0]->val, (*lowBonuses)[0]->val);
		EXPECT_GT((*lowBonuses)[0]->turnsRemain, 2); // Existing Extend Spell applies to the fixed base.
		EXPECT_GT((*highBonuses)[0]->turnsRemain, (*lowBonuses)[0]->turnsRemain);
		const auto profile = newHorizonsMetamagic::profile(attackerSideHero);
		EXPECT_EQ(low->metamagicMarks(BattleSide::ATTACKER), newHorizonsMetamagic::marksAfterResolvedSpell(profile, 0));
		EXPECT_EQ(high->metamagicMarks(BattleSide::ATTACKER), newHorizonsMetamagic::marksAfterResolvedSpell(profile, 5));
		EXPECT_EQ(decisionSnapshot(), before); // Cast effects and mark updates stayed on the detached branch.
	}
	void prepareEventStatuses()
	{
		// The same canonical trigger payloads as the existing physical-projection
		// fixtures: active statuses must not silently disable the optimization.
		const auto attach = [&](std::string_view source, std::string_view script, int value, JsonNode parameters)
		{
			auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
				BonusSource::SPELL_EFFECT, value, BonusSourceID(SpellID(SpellID::decode(std::string(source)))),
				BonusSubtypeID(ScriptID(ScriptID::decode(std::string(script)))));
			bonus->turnsRemain = 3;
			bonus->parameters = std::make_shared<BonusParameters>(parameters);
			active->addNewBonus(bonus);
		};
		JsonNode focus;
		focus["beneficiarySide"].Integer() = static_cast<int>(BattleSide::ATTACKER);
		attach(newHorizonsSorcery::FOCUS_MAGIC_SPELL, newHorizonsSorcery::FOCUS_MAGIC_TRIGGER, 2000, focus);
		JsonNode pain;
		pain["casterSide"].Integer() = static_cast<int>(BattleSide::DEFENDER);
		pain["damageSharePercent"].Integer() = 10;
		attach("new-horizons:hexOfPain", "core:hexOfPain", 20, pain);
		ASSERT_EQ(active->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER)->size(), 2u);
	}
	std::vector<std::byte> decisionSnapshot()
	{
		const auto live = callback->getBattle(BattleID(0));
		auto bytes = heroSelectionTest::snapshot(*live, *gameHandler->randomizer,
			dynamic_cast<CRandomGenerator &>(*getRNG()));
		appendSpellReceipts(bytes, *live);
		return bytes;
	}
	void recordDecision(const std::vector<std::byte> & before, const Scores & scores,
		const std::vector<std::byte> & hero, const std::vector<std::byte> & unit, bool accepted)
	{
		std::ostringstream rows;
		for(const auto & [ordinal, command, spell, overcharge, bits] : scores.rows)
			rows << ordinal << ',' << static_cast<int>(command) << ',' << spell.getNum()
				<< ',' << overcharge << ',' << bits << ';';
		RecordProperty("DecisionRows", rows.str());
		RecordProperty("DecisionStateAndRng", byteHex(before));
		RecordProperty("DecisionHeroAction", byteHex(hero));
		RecordProperty("DecisionUnitAction", byteHex(unit));
		RecordProperty("DecisionAccepted", accepted ? 1 : 0);
		RecordProperty("DecisionOrdinal", std::to_string(scores.selection->ordinal));
		RecordProperty("DecisionDisposition", static_cast<int>(scores.selection->disposition));
		RecordProperty("DecisionBaselineBits", std::to_string(std::bit_cast<uint32_t>(scores.selection->baseline)));
	}
	void checkCurrentOrderCandidates(const Scores & scores)
	{
		std::array<size_t, 3> arrowOvercharges{};
		for(const auto & [ordinal, command, spell, overcharge, bits] : scores.rows)
		{
			EXPECT_NE(command, HeroCommand::FLANK);
			EXPECT_NE(command, HeroCommand::PROTECT);
			if(spell == SpellID::MAGIC_ARROW)
			{
				ASSERT_GE(overcharge, 0);
				ASSERT_LT(static_cast<size_t>(overcharge), arrowOvercharges.size());
				++arrowOvercharges[overcharge];
			}
		}
		EXPECT_EQ(arrowOvercharges, (std::array<size_t, 3>{2, 2, 2}));
		for(const auto & action : callback->submitted)
		{
			EXPECT_FALSE(action.metamagicFollowup);
			EXPECT_FALSE(action.metamagicGrand);
		}
	}
	void compare()
	{
		heroTimingTest::Capture timing;
		const auto before = decisionSnapshot();
		int64_t disabledFullCalls = 0;
		int64_t disabledCachedCalls = 0;
		std::vector<Scores::Row> expected;
		std::vector<std::byte> expectedHero, expectedUnit;
		std::optional<heroActionSelection::SelectionValue> expectedSelection;
		bool expectedAccepted = false;
		for(const bool enabled : {false, true})
		{
			SCOPED_TRACE(enabled);
			callback->submitted.clear();
			BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
				BattleSide::ATTACKER, 1.0f, 2);
			const auto unit = evaluator.selectStackAction(active);
			EXPECT_EQ(decisionSnapshot(), before);
			Scores scores;
			VinesCandidateValueReuse::TestControl control{enabled};
			const auto timingRowsBefore = timing.records().size();
			const bool accepted = evaluator.attemptCastingSpell(active, true, &scores, &control);
			EXPECT_EQ(decisionSnapshot(), before); // Full state, bonuses, Orders, allowances, mana and BOTH RNG.
			ASSERT_EQ(callback->submitted.size(), accepted ? 1u : 0u);
			ASSERT_TRUE(scores.selection);
			ASSERT_FALSE(scores.rows.empty());
			if(useRevisedOrders)
				ASSERT_NO_FATAL_FAILURE(checkCurrentOrderCandidates(scores));
			if(heroTimingTest::optedIn())
			{
				const auto rows = timing.records();
				ASSERT_EQ(rows.size(), timingRowsBefore + 1);
				const auto & row = rows.back();
				EXPECT_EQ(row.at("candidates"), static_cast<int64_t>(scores.rows.size()));
				EXPECT_EQ(row.at("loop_joined"), 1);
				EXPECT_EQ(row.at("loop_threw"), 0);
				const auto full = row.at("continuation_full_calls");
				const auto cached = row.at("continuation_cached_calls");
				if(!enabled)
				{
					disabledFullCalls = full;
					disabledCachedCalls = cached;
				}
				else
				{
					EXPECT_GT(disabledFullCalls, full);
					EXPECT_EQ(disabledFullCalls - full, static_cast<int64_t>(control.hits));
					EXPECT_EQ(disabledCachedCalls, cached);
				}
				const std::string prefix = enabled ? "VinesEnabled" : "VinesDisabled";
				RecordProperty(prefix + "FullCalls", std::to_string(full));
				RecordProperty(prefix + "CachedCalls", std::to_string(cached));
				RecordProperty(prefix + "ReuseHits", std::to_string(control.hits));
				RecordProperty(prefix + "CandidateCount", std::to_string(scores.rows.size()));
				RecordProperty(prefix + "Timing", timing.rows().back());
			}
			const auto hero = projectedHitTest::serialized(callback->submitted);
			if(!enabled)
			{
				expected = scores.rows;
				expectedHero = hero;
				expectedUnit = projectedHitTest::serialized(unit);
				expectedSelection = scores.selection;
				expectedAccepted = accepted;
				EXPECT_EQ(control.hits, 0u);
			}
			else
			{
				EXPECT_GT(control.hits, 0u); // A parity-only test cannot silently pass all fallbacks.
				EXPECT_EQ(accepted, expectedAccepted);
				EXPECT_EQ(scores.rows, expected); // Every candidate identity/order and final float bit.
				EXPECT_EQ(hero, expectedHero); // Original submitted footprint, not a representative aim.
				EXPECT_EQ(projectedHitTest::serialized(unit), expectedUnit);
				EXPECT_EQ(scores.selection->ordinal, expectedSelection->ordinal);
				EXPECT_EQ(scores.selection->disposition, expectedSelection->disposition);
				EXPECT_EQ(std::bit_cast<uint32_t>(scores.selection->baseline),
					std::bit_cast<uint32_t>(expectedSelection->baseline));
				recordDecision(before, scores, hero, expectedUnit, accepted);
			}
		}
	}
};

TEST_F(VinesCandidateValueReuseAITest, AllCandidateFloatBitsActionsFullStateAndBothRngMatch)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(compare());
}

TEST_F(VinesCandidateValueReuseAITest, PerksMovementFocusMagicAndHexOfPainStillReuseWithExactParity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(compare());
}

TEST_F(VinesCandidateValueReuseAITest, CurrentRecipientMarksExtendAndLingeringKeepEveryScoreAndReuse)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(prepareCurrentMarkedRecipients());
	ASSERT_NO_FATAL_FAILURE(checkCurrentMarkedProjection());
	ASSERT_NO_FATAL_FAILURE(compare()); // Same ordered ledger, score bits, choices, state/RNG and positive hits.
}

TEST_F(VinesCandidateValueReuseAITest, CurrentOrdersMarksExtendAndLingeringKeepEveryScoreAndReuse)
{
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_EQ(battle()->getHeroCommandRules()["rulesetVersion"].Integer(), 4);
	ASSERT_TRUE(heroCommands::isRevisedRules(battle()->getHeroCommandRules()));
	EXPECT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), HeroCommand::FLANK));
	EXPECT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), HeroCommand::PROTECT));
	ASSERT_EQ(attackerSideHero->getPerkState().rules["metamagicPolicy"]["version"].Integer(), 2);
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), 3);
	ASSERT_NO_FATAL_FAILURE(prepareCurrentMarkedRecipients());
	EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), 0);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), 0);
	EXPECT_FALSE(battle()->battleCanUseMetamagicFollowup(BattleSide::ATTACKER));
	const auto counts = battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER);
	EXPECT_EQ(counts.heroActions, 1u);
	EXPECT_EQ(counts.spellActions, 0u);
	EXPECT_EQ(counts.orderActions, 0u);
	ASSERT_TRUE(newHorizonsMagic::magicArrowOverchargeEnabled(battle()->getMagicRules(), SpellID::MAGIC_ARROW));
	ASSERT_NO_FATAL_FAILURE(checkCurrentMarkedProjection());
	// Same observer, every ordered candidate/float bit, action, full state/BOTH RNG and positive reuse oracle.
	ASSERT_NO_FATAL_FAILURE(compare());
	RecordProperty("PolicyVersions", "Orders4/Metamagic2/Magic3");
	RecordProperty("FixtureScope", "Authored current-rules mechanism; direct Expert Metamagic, not fresh hero acquisition");
}

TEST_F(VinesCandidateValueReuseAITest, ShippingConsumerWallSamplesKeepDecisionStateAndRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto before = decisionSnapshot();
	std::vector<std::byte> expectedHero, expectedUnit;
	bool expectedAccepted = false;
	std::ostringstream samples;
	constexpr int sampleCount = 8;
	for(int sample = 0; sample != sampleCount; ++sample)
	{
		SCOPED_TRACE(sample);
		callback->submitted.clear();
		BattleAction unit;
		bool accepted;
		const auto started = std::chrono::steady_clock::now();
		{
			BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
				BattleSide::ATTACKER, 1.0f, 2);
			unit = evaluator.selectStackAction(active);
			accepted = evaluator.attemptCastingSpell(active);
		}
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - started).count();
		samples << (sample ? "," : "") << elapsed;
		// Serialization and parity observations are outside the measured consumer.
		EXPECT_EQ(decisionSnapshot(), before);
		ASSERT_EQ(callback->submitted.size(), accepted ? 1u : 0u);
		const auto hero = projectedHitTest::serialized(callback->submitted);
		const auto ordinary = projectedHitTest::serialized(unit);
		if(sample == 0)
		{
			expectedHero = hero;
			expectedUnit = ordinary;
			expectedAccepted = accepted;
		}
		else
		{
			EXPECT_EQ(hero, expectedHero);
			EXPECT_EQ(ordinary, expectedUnit);
			EXPECT_EQ(accepted, expectedAccepted);
		}
	}
	RecordProperty("WallSamplesMicroseconds", samples.str());
	RecordProperty("WallSampleCount", sampleCount);
	RecordProperty("InitialStateAndRng", byteHex(before));
	RecordProperty("HeroAction", byteHex(expectedHero));
	RecordProperty("UnitAction", byteHex(expectedUnit));
	RecordProperty("Accepted", expectedAccepted ? 1 : 0);
}
