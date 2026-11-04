/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "HeroActionSelectionObserver.h"
#include "../../AI/BattleAI/BattleEvaluatorSpellProjection.h"
#include "../../AI/BattleAI/HeroActionUtility.h"
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"

#include <limits>
#include "../../lib/spells/MagicalDamageReduction.h"

#include <bit>
#include <cmath>
#include <tuple>
#include "../../AI/BattleAI/HeroActionContinuation.h"
#include "../../AI/BattleAI/HeroActionTiming.h"
#include "../../AI/BattleAI/VinesCandidateValueReuse.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include <vcmi/ServerCallback.h>

namespace
{
class OrdinaryStrikeEvaluatorIntegrationTest : public heroQueueTest::HeroQueueSelectorTest
{
protected:
	int wingbeatPercent = 15;

	void mapLoaded(CMap * map) override
	{
		HeroQueueSelectorTest::mapLoaded(map);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]["wingbeatInitiativePercent"].Integer() = wingbeatPercent;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}

	void prepare(bool volley = false, int mdr = 50, bool markedArrow = false)
	{
		ASSERT_NO_FATAL_FAILURE(world());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(markedArrow ? "core:neela"
			: (volley ? "core:theodorus" : "core:aeris"))));
		ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
		if(markedArrow)
		{
			ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::TOWER);
			ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:metamagic",
				"new-horizons:metamagic.arcaneConjuration", MasteryLevel::BASIC));
		}
		ASSERT_NO_FATAL_FAILURE(startBattle());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		active = addStack(BattleSide::ATTACKER, creatureByName(volley ? "core:archMage" : "core:silverPegasus"), BattleHex(3, 5), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), volley ? BattleHex(12, 5) : BattleHex(4, 5), 1000);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), volley ? BattleHex(3, 8) : BattleHex(6, 5), 1000);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(actor, nullptr);
		for(const auto & [unit, initiative] : {std::pair{active, 60}, {enemy, 20}, {actor, 18}})
		{
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
				effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
					BonusSource::OTHER, initiative, BonusSourceID(), BonusSubtypeID(), bound));
			effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE,
				BonusSource::OTHER, 1, BonusSourceID()));
		}
		effect(active, Bonus(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION,
			BonusSource::OTHER, 1, BonusSourceID()));
		if(volley && mdr)
			effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION,
				BonusSource::OTHER, mdr, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_EQ(battle()->battleActiveUnit(), active);
		ASSERT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
		ASSERT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::DEFENDER));
		callback = std::make_shared<heroQueueTest::QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		// Warm native lazy queries before the isolation snapshot.
		for(const auto * unit : live->battleGetAllUnits(false))
			(void)unit->ableToRetaliate();
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}

	void characterizePublicQueue(bool retainBeforeCandidateScoring)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto turns = queue();
		ASSERT_FALSE(turns.empty());
		ASSERT_EQ(turns.front(), (std::vector<uint32_t>{active->unitId(), enemy->unitId(), actor->unitId()}));
		ASSERT_TRUE(battle()->getHeroActionAllowances(BattleSide::DEFENDER).eligibleAllowance(
			HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound()));
		DamageCache previewCache;
		PotentialTargets opening(child->battleGetUnitByID(active->unitId()), previewCache, child);
		ASSERT_FALSE(opening.possibleAttacks.empty());
		const auto selected = opening.bestAction();
		ASSERT_EQ(selected.attack.defender->unitId(), enemy->unitId());
		ASSERT_FALSE(selected.attack.shooting);
		ASSERT_TRUE(selected.effectPreview || !retainBeforeCandidateScoring);
		if(selected.effectPreview)
			EXPECT_EQ(selected.effectPreview->battleGetUnitByID(enemy->unitId())->getInitiative(),
				retainBeforeCandidateScoring ? 17 : 20);
		const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
		ASSERT_TRUE(arrow->canBeCast(battle(), spells::Mode::HERO, attackerSideHero));
		spells::BattleCast legalArrow(battle(), attackerSideHero, spells::Mode::HERO, arrow);
		ASSERT_TRUE(arrow->battleMechanics(&legalArrow)->canBeCastAt({spells::Destination(enemy)}));
		ASSERT_GT(selected.affectedUnits.front()->getAvailableHealth(), 0);
		ASSERT_LT(selected.affectedUnits.front()->getAvailableHealth(), enemy->getAvailableHealth());
		ASSERT_GT(battle()->calculateDmgRange(BattleAttackInfo(actor, enemy, 0, false)).damage.min,
			enemy->getAvailableHealth());
		const auto before = observation();
		std::string firstRequest;
		size_t firstCount = 0;
		heroActionSelection::CandidateValue firstBest{};
		for(int repeat = 0; repeat < 2; ++repeat)
		{
			callback->heroRequests.clear();
			DamageCache cache;
			cache.buildDamageCache(child, BattleSide::ATTACKER);
			BattleEvaluator evaluator(environment, callback, child, cache, active,
				PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
			(void)evaluator.selectStackAction(active);
			ASSERT_TRUE(evaluator.canCastSpell());
			heroSelectionTest::Observer observer;
			const bool submitted = evaluator.attemptCastingSpell(active, true, &observer);
			EXPECT_EQ(observation(), before); // Live, ancestors, sibling, RNG and server traffic.
			EXPECT_TRUE(callback->unitRequests.empty());
			if(retainBeforeCandidateScoring)
			{
				EXPECT_FALSE(submitted);
				EXPECT_EQ(observer.count, 0u);
				EXPECT_EQ(observer.selections, 0u);
				EXPECT_TRUE(callback->heroRequests.empty());
			}
			else
			{
				ASSERT_GT(observer.count, 0u);
				ASSERT_TRUE(observer.hasBest);
				ASSERT_TRUE(observer.selection);
				EXPECT_FALSE(observer.invalidOrdinal);
				EXPECT_FALSE(observer.nonfinite);
				EXPECT_EQ(observer.selection->ordinal, observer.best.ordinal); // Existing strict first maximum.
				EXPECT_EQ(observer.selections, 1u);
				EXPECT_EQ(callback->heroRequests.size(), submitted ? 1u : 0u);
			}
			const auto request = heroQueueTest::wire(callback->heroRequests);
			if(repeat == 0)
			{
				firstRequest = request;
				firstCount = observer.count;
				firstBest = observer.best;
			}
			else
			{
				EXPECT_EQ(request, firstRequest);
				EXPECT_EQ(observer.count, firstCount);
				EXPECT_EQ(observer.best.ordinal, firstBest.ordinal);
				EXPECT_EQ(std::bit_cast<uint32_t>(observer.best.value), std::bit_cast<uint32_t>(firstBest.value));
			}
		}
	}

	void characterizeVolley(int mdr)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(true, mdr));
		const auto outside = observation();
		DamageCache cache;
		PotentialTargets targets(child->battleGetUnitByID(active->unitId()), cache, child);
		ASSERT_FALSE(targets.possibleAttacks.empty());
		const auto selected = targets.bestAction();
		ASSERT_TRUE(selected.attack.shooting);
		ASSERT_EQ(selected.attack.defender->unitId(), enemy->unitId());
		ASSERT_TRUE(selected.effectPreview);
		ASSERT_EQ(spells::targetMdrPenetrations(*selected.effectPreview->battleGetUnitByID(enemy->unitId())),
			(std::vector<int>{20}));
		auto replay = std::make_shared<HypotheticBattle>(environment.get(), child);
		BattleExchangeVariant exchange;
		exchange.trackAttack(selected, replay, cache); // Real selected receipt, no injected effectPreview.
		const auto * replayedVictim = replay->battleGetUnitByID(enemy->unitId());
		ASSERT_GT(replayedVictim->getAvailableHealth(), 0);
		ASSERT_LT(replayedVictim->getAvailableHealth(), enemy->getAvailableHealth());
		ASSERT_EQ(spells::targetMdrPenetrations(*replayedVictim), (std::vector<int>{20}));
		auto clean = std::make_shared<HypotheticBattle>(environment.get(), replay);
		clean->getForUpdate(enemy->unitId())->removeUnitBonus(CSelector(newHorizonsOrdinaryStrike::isStrikeEffect));
		ASSERT_TRUE(spells::targetMdrPenetrations(*clean->battleGetUnitByID(enemy->unitId())).empty());
		ASSERT_EQ(clean->battleGetUnitByID(enemy->unitId())->getAvailableHealth(), replayedVictim->getAvailableHealth());
		int64_t damage[2]{};
		float utility[2]{};
		int index = 0;
		for(const auto & source : {clean, replay})
		{
			auto castState = std::make_shared<HypotheticBattle>(environment.get(), source);
			PossibleSpellcast spell;
			spell.spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
			spell.dest = {spells::Destination(enemy)};
			const auto admission = heroActionProjection::admitCandidate(*castState, BattleSide::ATTACKER, spell);
			ASSERT_TRUE(admission.admitted);
			uint32_t targetId = std::numeric_limits<uint32_t>::max();
			const auto projection = battleEvaluatorProjection::projectSpell(*castState, *source,
				attackerSideHero, BattleSide::ATTACKER, spell, admission, targetId, {});
			ASSERT_TRUE(projection.accepted);
			ASSERT_EQ(targetId, enemy->unitId());
			const auto * oldVictim = source->battleGetUnitByID(targetId);
			const auto * newVictim = castState->battleGetUnitByID(targetId);
			damage[index] = oldVictim->getAvailableHealth() - newVictim->getAvailableHealth();
			ASSERT_GT(damage[index], 0);
			ASSERT_TRUE(newVictim->alive()); // No health-cap artifact in the relative comparison.
			DamageCache spellCache;
			BattleExchangeEvaluator scorer(live, environment, 1.0f, 2);
			heroActionUtility::ImmediateSignedUtility immediate{};
			ASSERT_TRUE(heroActionUtility::accumulateHealthDelta({nullptr, oldVictim, newVictim,
				oldVictim->getAvailableHealth(), newVictim->getAvailableHealth(), false}, spell,
				PlayerColor(0), spellCache, castState, scorer, immediate));
			utility[index++] = heroActionUtility::composeAbsoluteUtility({0}, immediate).value;
		}
		if(mdr > 0)
		{
			EXPECT_GT(damage[1], damage[0]);
			EXPECT_GT(utility[1], utility[0]);
		}
		else
		{
			EXPECT_EQ(damage[1], damage[0]);
			EXPECT_EQ(std::bit_cast<uint32_t>(utility[1]), std::bit_cast<uint32_t>(utility[0]));
		}
		EXPECT_EQ(spells::targetMdrPenetrations(*replayedVictim), (std::vector<int>{20}));
		EXPECT_EQ(observation(), outside);
	}
};

// Test-local typed forwarding only: observe the normalized injury at the real
// native castEval boundary, and forward every completion hook unchanged.
class ArrowInjuryRecorder final : public ServerCallback
{
	ServerCallback & delegate;
public:
	std::vector<StacksInjured> injuries;
	std::vector<BattleUnitsChanged> markUpdates;
	uint64_t otherStatePackets = 0, completed = 0;
	explicit ArrowInjuryRecorder(ServerCallback & delegate) : delegate(delegate) {}
	void complain(const std::string & text) override { delegate.complain(text); }
	bool describeChanges() const override { return delegate.describeChanges(); }
	vstd::RNG * getRNG() override { return delegate.getRNG(); }
	bool rollCombatAbility(const IBattleInfoCallback & b, const battle::Unit & a, int p) override
	{ return delegate.rollCombatAbility(b, a, p); }
	bool rollHostileCombatAbility(const IBattleInfoCallback & b, const battle::Unit & a,
		const battle::Unit & t, int p) override { return delegate.rollHostileCombatAbility(b, a, t, p); }
	bool resolveAdverseCombatRoll(const BattleID & b, BattleSide s, bool stochastic, bool adverse,
		const std::function<bool()> & draw) override
	{ return delegate.resolveAdverseCombatRoll(b, s, stochastic, adverse, draw); }
	void apply(CPackForClient & p) override { ++otherStatePackets; delegate.apply(p); }
	void apply(BattleLogMessage & p) override { delegate.apply(p); }
	void apply(BattleStackMoved & p) override { ++otherStatePackets; delegate.apply(p); }
	void apply(BattleUnitsChanged & p) override { markUpdates.push_back(p); delegate.apply(p); }
	void apply(SetStackEffect & p) override { ++otherStatePackets; delegate.apply(p); }
	void apply(StacksInjured & p) override { injuries.push_back(p); delegate.apply(p); }
	void apply(BattleObstaclesChanged & p) override { ++otherStatePackets; delegate.apply(p); }
	void apply(CatapultAttack & p) override { ++otherStatePackets; delegate.apply(p); }
	void recordCrossSchoolFormulaCast(BattleSide s, const newHorizonsCrossSchoolFormula::Receipt & r) override
	{ delegate.recordCrossSchoolFormulaCast(s, r); }
	void recordResolvedSpellRecipient(uint32_t id) override { delegate.recordResolvedSpellRecipient(id); }
	void recordCompletedWisdomSpellCast(BattleSide s, const SpellID & spell, int32_t round, bool automatic) override
	{ delegate.recordCompletedWisdomSpellCast(s, spell, round, automatic); }
	void recordCompletedHeroSpellCast(BattleSide s) override
	{ ++completed; delegate.recordCompletedHeroSpellCast(s); }
	void recordCompletedHeroSpellCast(BattleSide s, int32_t level) override
	{ ++completed; delegate.recordCompletedHeroSpellCast(s, level); }
	void recordExtendSpellCast(BattleSide s) override { delegate.recordExtendSpellCast(s); }
	void recordCompletedAutomaticHeroSpellCast(BattleSide s, const SpellID & spell,
		const spells::AutomaticHeroCastReceipt & receipt) override
	{ delegate.recordCompletedAutomaticHeroSpellCast(s, spell, receipt); }
};

// Complete candidate identity/value oracle for this finite reuse comparison.
class ArrowScores final : public heroActionSelection::Observer
{
public:
	using Row = std::tuple<size_t, HeroCommand, SpellID, int32_t, uint32_t, heroActionSelection::CandidateIdentity>;
	std::vector<Row> rows;
	std::optional<heroActionSelection::SelectionValue> selection;
	bool failed = false;
	ArrowScores() { rows.reserve(256); }
	void candidate(heroActionSelection::CandidateValue value) noexcept override
	{
		try { rows.emplace_back(value.ordinal, value.command, value.spell, value.overcharge,
			std::bit_cast<uint32_t>(value.value), heroActionSelection::CandidateIdentity{}); }
		catch(...) { failed = true; }
	}
	bool observeIdentity() const noexcept override { return true; }
	void candidateIdentity(size_t ordinal, heroActionSelection::CandidateIdentity identity) noexcept override
	{
		if(rows.empty() || std::get<0>(rows.back()) != ordinal) failed = true;
		else std::get<5>(rows.back()) = std::move(identity);
	}
	void selected(heroActionSelection::SelectionValue value) noexcept override { selection = value; }
};

class MagicArrowContinuationEquivalenceTest : public OrdinaryStrikeEvaluatorIntegrationTest
{
	std::string completedArrowState(const HypotheticBattle & view)
	{
		// Existing fixture captures full per-unit CUnitState and all bonus values,
		// Orders, allowances, round, active actor, initiative and cast counts.
		std::string result = state(view);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			result += heroQueueTest::wire(view.getWisdomCombatState(side));
			result += heroQueueTest::wire(view.getCrossSchoolFormulaState(side));
			result += heroQueueTest::wire(view.getSpellResponseState(side));
			result += heroQueueTest::wire(view.getOverwhelmingFormulaState(side));
			result += heroQueueTest::wire(view.getMetamagicSequenceSpells(side));
			result += heroQueueTest::wire(std::vector<int64_t>{view.getExtendSpellLastRound(side),
				view.getMetamagicPendingCount(side), view.getMetamagicUsesConsumed(side),
				view.getMetamagicGrandUsed(side), view.getMetamagicFirstSpell(side).getNum(),
				view.getMetamagicFirstTargetUnitId(side), view.getMetamagicFirstCounterspellNegated(side),
				view.getMetamagicCountersequenceArmed(side), view.getCounterspellArmed(side)});
			result += heroQueueTest::wire(view.getProjectedMana(side));
		}
		return result;
	}
protected:
	void comparePublicReuse(bool manaDrain)
	{
		wingbeatPercent = 0;
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(prepare(false, 50, true));
		ASSERT_NO_FATAL_FAILURE(injure(enemy, 1));
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 5), 1000), nullptr);
		if(manaDrain)
			effect(actor, Bonus(BonusDuration::ONE_BATTLE, BonusType::MANA_DRAIN, BonusSource::OTHER, 1, BonusSourceID()));
		for(const auto * unit : live->battleGetAllUnits(false)) (void)unit->ableToRetaliate();
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto outside = observation();
		const auto parentBefore = completedArrowState(*parent), siblingBefore = completedArrowState(*sibling);
		const auto worldBefore = gameState()->saveToMemory();
		const auto defaultRng = heroQueueTest::wire(CRandomGenerator::getDefault());
		std::optional<std::vector<ArrowScores::Row>> expected;
		std::optional<heroActionSelection::SelectionValue> selected;
		std::string requested;
		bool submittedBefore = false;
		for(const bool enabled : {false, true})
		{
			callback->heroRequests.clear();
			callback->unitRequests.clear();
			child = std::make_shared<HypotheticBattle>(environment.get(), parent);
			DamageCache cache;
			cache.buildDamageCache(child, BattleSide::ATTACKER);
			BattleEvaluator evaluator(environment, callback, child, cache, active,
				PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
			(void)evaluator.selectStackAction(active);
			const auto childBefore = completedArrowState(*child);
			ArrowScores scores;
			arrowContinuation::Reuse::TestControl control{enabled};
			const bool submitted = evaluator.attemptCastingSpell(active, true, &scores,
				nullptr, nullptr, nullptr, nullptr, &control);
			ASSERT_FALSE(scores.failed);
			ASSERT_FALSE(scores.rows.empty());
			ASSERT_TRUE(scores.selection);
			const auto requests = heroQueueTest::wire(callback->heroRequests);
			if(!enabled)
			{
				expected = scores.rows; selected = scores.selection;
				requested = requests; submittedBefore = submitted;
				EXPECT_EQ(control.hits, 0u);
			}
			else
			{
				EXPECT_EQ(scores.rows, *expected); // Every identity/order/final value bit.
				EXPECT_EQ(scores.selection->ordinal, selected->ordinal);
				EXPECT_EQ(scores.selection->disposition, selected->disposition);
				EXPECT_EQ(std::bit_cast<uint32_t>(scores.selection->baseline), std::bit_cast<uint32_t>(selected->baseline));
				EXPECT_EQ(submitted, submittedBefore);
				EXPECT_EQ(requests, requested); // Complete chosen action, including original charge/aim.
				if(manaDrain) EXPECT_EQ(control.hits, 0u);
				else EXPECT_GT(control.hits, 0u); // Hits are admitted only at the actual FULL boundary.
				RecordProperty("ContinuationReuseHits", std::to_string(control.hits));
			}
			EXPECT_TRUE(callback->unitRequests.empty());
			EXPECT_EQ(observation(), outside);
			EXPECT_EQ(completedArrowState(*parent), parentBefore);
			EXPECT_EQ(completedArrowState(*sibling), siblingBefore);
			EXPECT_EQ(completedArrowState(*child), childBefore);
			EXPECT_EQ(gameState()->saveToMemory(), worldBefore);
			EXPECT_EQ(heroQueueTest::wire(CRandomGenerator::getDefault()), defaultRng);
		}
	}
	void characterize(int nativeCharge)
	{
		wingbeatPercent = 0;
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(prepare(false, 50, true));
		ASSERT_NO_FATAL_FAILURE(injure(enemy, 1));
		// Killing the Arrow victim must leave a meaningful ordinary FULL search.
		const auto * reserve = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 5), 1000);
		ASSERT_NE(reserve, nullptr);
		for(const auto * unit : live->battleGetAllUnits(false)) (void)unit->ableToRetaliate();
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
		ASSERT_TRUE(newHorizonsMagic::magicArrowOverchargeEnabled(parent->getMagicRules(), arrow->getId()));
		ASSERT_TRUE(VinesCandidateValueReuse::scoringScriptsSupported(*parent));
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			ASSERT_FALSE(parent->getCounterspellArmed(side));
			ASSERT_FALSE(parent->getMetamagicCountersequenceArmed(side));
			ASSERT_TRUE(parent->getHeroOrderStates(side).empty()); // No active Order Divine Fury.
		}
		for(const auto * unit : parent->battleGetAllUnits(false))
		{
			ASSERT_NE(unit->creatureId(), creatureByName("core:imp"));
			ASSERT_NE(unit->creatureId(), creatureByName("core:familiar"));
			ASSERT_FALSE(unit->hasBonusOfType(BonusType::MANA_DRAIN));
		}
		ASSERT_FALSE(enemy->hasBonusOfType(BonusType::REBIRTH));
		const auto outside = observation();
		const auto defaultRng = heroQueueTest::wire(CRandomGenerator::getDefault());
		DamageCache original;
		original.buildDamageCache(parent, BattleSide::ATTACKER);
		const auto originalBefore = original.captureSnapshot();
		std::string expectedInjury, expectedPost, expectedMarks, expectedNativeInjuries;
		uint32_t expectedContinuation = 0;
		int costs[2]{};
		int64_t raw[2]{};
		for(int charge = 0; charge < 2; ++charge)
		{
			PossibleSpellcast candidate;
			candidate.spell = arrow;
			candidate.dest = {spells::Destination(enemy)};
			candidate.spellOvercharge = charge;
			auto projected = std::make_shared<HypotheticBattle>(environment.get(), parent);
			const auto admission = heroActionProjection::admitCandidate(*projected, BattleSide::ATTACKER, candidate);
			ASSERT_TRUE(admission.admitted);
			ASSERT_FALSE(admission.counterspell.wardActive);
			spells::BattleCast cast(projected.get(), attackerSideHero, spells::Mode::HERO, arrow);
			cast.setOvercharge(charge);
			cast.setMetamagicTargetUnitId(enemy->unitId());
			auto mechanics = arrow->battleMechanics(&cast);
			const spells::Target target{spells::Destination(projected->battleGetUnitByID(enemy->unitId()))};
			ASSERT_TRUE(mechanics->canBeCastAt(target));
			EXPECT_EQ(mechanics->getOvercharge(), charge);
			ASSERT_NE(mechanics->getMetamagicCastProfile(), nullptr);
			costs[charge] = mechanics->getTargetAwareManaCost(target);
			raw[charge] = mechanics->adjustEffectValue(target.front().unitValue);
			ASSERT_GT(raw[charge], 1);
			ArrowInjuryRecorder recorder(*projected->getServerCallback());
			mechanics->castEval(&recorder, target); // A fresh real effect execution at EACH charge.
			ASSERT_EQ(recorder.injuries.size(), 1u);
			ASSERT_EQ(recorder.injuries.front().stacks.size(), 1u);
			ASSERT_EQ(recorder.otherStatePackets, 0u); // This finite fixture admits no other state pack.
			EXPECT_EQ(recorder.completed, 1u);
			ASSERT_EQ(recorder.markUpdates.size(), 1u);
			ASSERT_EQ(recorder.markUpdates.front().changedStacks.size(), 1u);
			EXPECT_EQ(recorder.markUpdates.front().changedStacks.front().id, enemy->unitId());
			EXPECT_EQ(projected->battleGetUnitByID(enemy->unitId())->metamagicMarks(BattleSide::ATTACKER), 1);
			const auto & injury = recorder.injuries.front().stacks.front();
			EXPECT_EQ(injury.stackAttacked, enemy->unitId());
			EXPECT_EQ(injury.damageAmount, 1);
			EXPECT_TRUE(injury.killed());
			ASSERT_TRUE(projected->projectAcceptedHeroSpell(BattleSide::ATTACKER, arrow->getId(),
				enemy->unitId(), false, false, false, false, *admission.spellAllowance, {}));
			// Cross-check the real projectSpell owner; the recorder does not replace
			// its accepted/mark/completed-cast path or provide a cache certificate.
			auto direct = std::make_shared<HypotheticBattle>(environment.get(), parent);
			const auto directAdmission = heroActionProjection::admitCandidate(*direct, BattleSide::ATTACKER, candidate);
			ASSERT_TRUE(directAdmission.admitted);
			uint32_t targetId = std::numeric_limits<uint32_t>::max();
			ASSERT_TRUE(battleEvaluatorProjection::projectSpell(*direct, *parent, attackerSideHero,
				BattleSide::ATTACKER, candidate, directAdmission, targetId, {}).accepted);
			ASSERT_EQ(completedArrowState(*direct), completedArrowState(*projected));
			EXPECT_EQ(targetId, enemy->unitId());
			EXPECT_FALSE(projected->battleGetUnitByID(enemy->unitId())->alive());
			EXPECT_TRUE(projected->battleGetUnitByID(reserve->unitId())->alive());
			const auto post = completedArrowState(*projected);
			const auto injuryBytes = heroQueueTest::wire(recorder.injuries);
			DamageCache safeCopy(original), cache(&safeCopy);
			cache.buildDamageCache(projected, BattleSide::ATTACKER);
			BattleExchangeEvaluator scorer(parent, environment, 1.0f, 2);
			heroActionUtility::AbsoluteContinuationUtility continuation{0};
			heroActionTiming::Continuation timing;
			heroActionUtility::evaluateContinuation(continuation, projected->getForUpdate(active->unitId()),
				true, CachedAttack{}, cache, projected, environment, 1.0f, 2, scorer, nullptr, &timing);
			ASSERT_EQ(timing.full.calls, 1u);
			ASSERT_EQ(timing.fullBestTarget.calls, 1u);
			ASSERT_TRUE(std::isfinite(continuation.value));
			ASSERT_NE(continuation.value, static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE));
			heroActionUtility::ImmediateSignedUtility immediate{};
			ASSERT_TRUE(heroActionUtility::accumulateHealthDelta({nullptr,
				parent->battleGetUnitByID(enemy->unitId()), projected->battleGetUnitByID(enemy->unitId()),
				1, 0, false}, candidate, PlayerColor(0), cache, projected, scorer, immediate));
			candidate.value = heroActionUtility::composeAbsoluteUtility(continuation, immediate).value;
			ASSERT_TRUE(std::isfinite(candidate.value));
			const auto bits = std::bit_cast<uint32_t>(continuation.value);
			RecordProperty("Charge" + std::to_string(charge) + "FinalCandidateBits",
				std::to_string(std::bit_cast<uint32_t>(candidate.value)));
			if(charge == 0)
			{
				expectedInjury = injuryBytes; expectedPost = post; expectedContinuation = bits;
				expectedMarks = heroQueueTest::wire(recorder.markUpdates);
				// Native cast() additionally sends an empty animation-completion event.
				auto nativePackets = recorder.injuries;
				StacksInjured completion;
				completion.battleID = BattleID(0);
				nativePackets.push_back(completion);
				expectedNativeInjuries = heroQueueTest::wire(nativePackets);
			}
			else
			{
				EXPECT_EQ(injuryBytes, expectedInjury); // ALL serialized injury fields, not HP alone.
				EXPECT_EQ(post, expectedPost); // This witness also keeps forecast Mana equal.
				EXPECT_EQ(heroQueueTest::wire(recorder.markUpdates), expectedMarks);
				EXPECT_EQ(bits, expectedContinuation);
			}
			RecordProperty("Charge" + std::to_string(charge) + "ContinuationBits", std::to_string(bits));
			RecordProperty("Charge" + std::to_string(charge) + "OwnManaCost", std::to_string(costs[charge]));
			EXPECT_EQ(observation(), outside);
			EXPECT_EQ(heroQueueTest::wire(CRandomGenerator::getDefault()), defaultRng);
			EXPECT_TRUE(original.matchesSnapshot(originalBefore));
		}
		EXPECT_GT(costs[1], costs[0]);
		EXPECT_GT(raw[1], raw[0]);
		RecordProperty("RawDamage0", std::to_string(raw[0]));
		RecordProperty("RawDamage1", std::to_string(raw[1]));
		// Each companion testcase starts the same native fixture and pays its OWN
		// charge. This does not assert equal native worlds with differing Mana.
		const auto manaBefore = attackerSideHero->getManaAvailable();
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = arrow->getId();
		action.spellOvercharge = nativeCharge;
		action.aimToUnit(enemy);
		server.injuries.clear();
		const auto actionsBefore = server.startedActions.size();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), costs[nativeCharge]);
		RecordProperty("AuthoritativeInjuryPackets", heroQueueTest::wire(server.injuries));
		std::string hitCounts;
		for(const auto & packet : server.injuries) hitCounts += std::to_string(packet.stacks.size()) + ",";
		RecordProperty("AuthoritativeInjuryPacketHitCounts", hitCounts);
		ASSERT_EQ(server.startedActions.size() - actionsBefore, 1u);
		EXPECT_EQ(server.startedActions.back().ba.actionType, EActionType::HERO_SPELL);
		EXPECT_EQ(server.startedActions.back().ba.spell, arrow->getId());
		EXPECT_EQ(server.startedActions.back().ba.spellOvercharge, nativeCharge);
		ASSERT_EQ(server.injuries.size(), 2u);
		ASSERT_EQ(server.injuries.front().stacks.size(), 1u);
		EXPECT_EQ(heroQueueTest::wire(std::vector<StacksInjured>{server.injuries.front()}), expectedInjury);
		EXPECT_EQ(server.injuries.back().battleID, BattleID(0));
		EXPECT_TRUE(server.injuries.back().stacks.empty());
		EXPECT_EQ(heroQueueTest::wire(server.injuries), expectedNativeInjuries);
		RecordProperty("AuthoritativeCharge", std::to_string(nativeCharge));
		RecordProperty("AuthoritativePaidMana", std::to_string(costs[nativeCharge]));
	}
};

TEST_F(MagicArrowContinuationEquivalenceTest, SaturatedOrdinaryArrowChargesKeepFullContinuationAndNativeZeroOverchargePayment)
{
	ASSERT_NO_FATAL_FAILURE(characterize(0));
}
TEST_F(MagicArrowContinuationEquivalenceTest, SaturatedOrdinaryArrowChargesKeepFullContinuationAndNativeOneOverchargePayment)
{
	ASSERT_NO_FATAL_FAILURE(characterize(1));
}

TEST_F(OrdinaryStrikeEvaluatorIntegrationTest, AerisRealHeroQueueFinishesBeforeCounterspellBoundary)
{
	ASSERT_NO_FATAL_FAILURE(characterizePublicQueue(true));
}

TEST_F(OrdinaryStrikeEvaluatorIntegrationTest, ZeroRiderRealSelectorStillScoresCandidatesAndPreservesFirstMaximum)
{
	wingbeatPercent = 0;
	ASSERT_NO_FATAL_FAILURE(characterizePublicQueue(false));
}

TEST_F(OrdinaryStrikeEvaluatorIntegrationTest, TheodorusSelectedShotReplayIncreasesLaterFriendlyHeroSpellUtility)
{
	ASSERT_NO_FATAL_FAILURE(characterizeVolley(50));
}

TEST_F(OrdinaryStrikeEvaluatorIntegrationTest, TheodorusSelectedShotReplayCannotCreateVulnerabilityAtZeroMdr)
{
	ASSERT_NO_FATAL_FAILURE(characterizeVolley(0));
}
}

TEST_F(MagicArrowContinuationEquivalenceTest, PublicArrowContinuationReusePreservesEveryCandidateAndChosenAction)
{
	ASSERT_NO_FATAL_FAILURE(comparePublicReuse(false));
}
TEST_F(MagicArrowContinuationEquivalenceTest, ManaDrainBearerRetainsEveryFullContinuation)
{
	ASSERT_NO_FATAL_FAILURE(comparePublicReuse(true));
}
