/*
 * ArcaneOverflowProjectionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/ArcaneOverflowValue.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include "../../AI/BattleAI/BattleSearchPolicy.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include <map>
#include <set>
#include <cmath>
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMetamagicOverflow.h"
#include "../../lib/combatScripts/nativeEvents/NativeEventCommon.h"

namespace heroQueueTest
{
class ArcaneOverflowProjectionTest : public HeroQueueSelectorTest
{
protected:
	int policyVersion = 5;
	CStack * collateral = nullptr;
	const SpellID spell{SpellID::MAGIC_ARROW};
	void mapLoaded(CMap * map) override
	{
		HeroQueueSelectorTest::mapLoaded(map);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		perks["metamagicPolicy"]["version"].Integer() = policyVersion;
		perks["metamagicPolicy"].Struct().erase("friendlyDamageReductionPercentByRank");
		perks["metamagicPolicy"].Struct().erase("debuffRetryChancePercentByRank");
		if(policyVersion == 5)
			perks["metamagicPolicy"]["arcaneOverflowPercentByRank"].Vector()
				= {JsonNode(0), JsonNode(25), JsonNode(50), JsonNode(75)};
		else perks["metamagicPolicy"].Struct().erase("arcaneOverflowPercentByRank");
		// This is a complete captured profile; missing historical fields must stay absent.
		perks.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void prepare(int rank = 3, bool lethal = true, bool secondEnemy = true)
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, spell, "new-horizons:sorceryMagic"));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
			rank, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(arena());
		if(secondEnemy)
			collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(9, 5), 1000);
		if(lethal) ASSERT_NO_FATAL_FAILURE(injure(enemy, 1));
		else attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
	std::optional<newHorizonsMetamagic::OverflowPreview> preview()
	{
		spells::BattleCast cast(live.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto native = spell.toSpell()->battleMechanics(&cast);
		return newHorizonsMetamagic::previewOverflow(*native, {spells::Destination(enemy)});
	}
};

TEST_F(ArcaneOverflowProjectionTest, ThreeNativeConditionalOutcomesAreAcceptedOnceAndDoNotTouchLiveWorldOrRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = gameState()->saveToMemory();
	const auto random = wire(*gameHandler->randomizer);
	const auto ambient = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto plan = preview();
	ASSERT_TRUE(plan);
	auto chances = *plan;
	// The weighting adapter sees a valid native receipt; these probabilities
	// deliberately exercise all three existing native conditional paths.
	chances.primaryResistancePercent = chances.secondaryResistancePercent = 50;
	auto work = std::make_shared<battleSearch::SearchBudget>(battleSearch::SearchLimits::full().decision);
	PossibleSpellcast candidate;
	candidate.spell = spell.toSpell(); candidate.dest = {spells::Destination(enemy)};
	int visits = 0;
	const auto value = arcaneOverflowAI::expectedLocalValue(chances, *work,
		[&](arcaneOverflowAI::Outcome outcome) -> std::optional<arcaneOverflowAI::LocalUtility>
		{
			++visits;
			auto branch = std::make_shared<HypotheticBattle>(environment.get(), live, nullptr, work);
			const auto admission = heroActionProjection::admitCandidate(*branch, BattleSide::ATTACKER, candidate);
			if(!admission.admitted || !admission.spellAllowance) return std::nullopt;
			spells::BattleCast cast(branch.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
			const auto native = spell.toSpell()->battleMechanics(&cast);
			using Native = newHorizonsMetamagic::OverflowProjectionOutcome;
			native->setMetamagicOverflowProjectionOutcome(outcome == arcaneOverflowAI::Outcome::PrimaryResisted
				? Native::PrimaryResisted : outcome == arcaneOverflowAI::Outcome::PrimaryOnly ? Native::SpillResisted : Native::BothLand);
			native->castEval(branch->getServerCallback(), {spells::Destination(branch->battleGetUnitByID(enemy->unitId()))});
			const bool accepted = branch->projectAcceptedHeroSpell(BattleSide::ATTACKER, spell, enemy->unitId(), *admission.spellAllowance);
			EXPECT_TRUE(accepted);
			EXPECT_FALSE(branch->prepareHeroSpellAllowance(BattleSide::ATTACKER, spell));
			const auto * primary = branch->battleGetUnitByID(enemy->unitId());
			const auto * secondary = branch->battleGetUnitByID(collateral->unitId());
			EXPECT_EQ(primary->alive(), outcome == arcaneOverflowAI::Outcome::PrimaryResisted);
			if(outcome == arcaneOverflowAI::Outcome::BothLand)
				EXPECT_EQ(secondary->getAvailableHealth(), collateral->getAvailableHealth() - plan->damageIfLands);
			else EXPECT_EQ(secondary->getAvailableHealth(), collateral->getAvailableHealth());
			for(const auto id : native->getResolvedHeroRecipients()) EXPECT_EQ(id, enemy->unitId());
			return arcaneOverflowAI::LocalUtility{1, 2};
		});
	ASSERT_TRUE(value);
	EXPECT_EQ(visits, 3);
	EXPECT_FLOAT_EQ(value->immediate, 1);
	EXPECT_FLOAT_EQ(value->completion, 2);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
	EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), ambient);
}
TEST_F(ArcaneOverflowProjectionTest, NonlethalNativeSpellHasNoOverflowExpectation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3, false));
	EXPECT_FALSE(preview());
}
TEST_F(ArcaneOverflowProjectionTest, LastEnemyHasNoSecondaryExpectation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3, true, false));
	EXPECT_FALSE(preview());
}
TEST_F(ArcaneOverflowProjectionTest, MissingRankDoesNotActivateOverflow)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0));
	EXPECT_FALSE(preview());
}
TEST_F(ArcaneOverflowProjectionTest, HistoricalPolicyFourDoesNotActivateOverflow)
{
	policyVersion = 4;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(preview());
}
TEST_F(ArcaneOverflowProjectionTest, BorrowedSecondaryPlanUsesCurrentLivingAuraPeersWithoutMaterializingTheRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::SPELL_RESISTANCE_AURA,
		BonusSource::OTHER, 60, BonusSourceID()));
	ASSERT_EQ(collateral->magicResistance(), 60);
	const auto random = wire(*gameHandler->randomizer);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), live);
	branch->useReferenceForecast();
	ASSERT_EQ(branch->battleGetUnitByID(collateral->unitId()), collateral);
	spells::BattleCast cast(branch.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto native = spell.toSpell()->battleMechanics(&cast);
	newHorizonsMetamagic::OverflowState state;
	state.primary = enemy->unitId(); state.footprint = enemy->getHexes(); state.percent = 75;
	const auto health = enemy->getAvailableHealth();
	state.observe({enemy->unitId(), health, health + 100, health, true});
	const JsonNode parameters;
	const CombatEventPayload payload;
	const nativeEvents::Context context{*branch->getServerCallback(), *branch, nullptr, nullptr, parameters, payload};
	nativeEvents::damage(context, *branch->battleGetUnitByID(enemy->unitId()), health, nullptr, true);
	ASSERT_FALSE(branch->battleGetUnitByID(enemy->unitId())->alive());
	ASSERT_TRUE(enemy->alive());
	ASSERT_EQ(branch->battleGetUnitByID(collateral->unitId()), collateral);
	EXPECT_EQ(collateral->magicResistance(), 60); // Borrowed source environment still sees its live primary.
	EXPECT_EQ(branch->battleGetMagicResistance(collateral), 0); // Current branch has no living aura provider.
	const auto plan = newHorizonsMetamagic::planOverflow(*branch, *native, state);
	ASSERT_TRUE(plan);
	EXPECT_EQ(plan->recipient, collateral->unitId());
	EXPECT_EQ(plan->secondaryResistancePercent, 0);
	EXPECT_EQ(plan->rawSpill, 75);
	EXPECT_EQ(plan->damageIfLands, 75);
	EXPECT_EQ(branch->battleGetUnitByID(collateral->unitId()), collateral);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
}
TEST_F(ArcaneOverflowProjectionTest, NativePostKillAuraPlanDrivesActualCandidateInsteadOfHoverEquality)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::SPELL_RESISTANCE_AURA,
		BonusSource::OTHER, 60, BonusSourceID()));
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
		BonusSource::OTHER, 500, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
	ASSERT_EQ(collateral->magicResistance(), 60);
	EXPECT_FALSE(preview()) << "UI suppresses an unrepresented primary-aura death forecast";
	auto work = std::make_shared<battleSearch::SearchBudget>(battleSearch::SearchLimits::full().decision);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), live, nullptr, work);
	spells::BattleCast cast(branch.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto native = spell.toSpell()->battleMechanics(&cast);
	native->setMetamagicOverflowProjectionOutcome(newHorizonsMetamagic::OverflowProjectionOutcome::BothLand);
	native->castEval(branch->getServerCallback(), {spells::Destination(branch->battleGetUnitByID(enemy->unitId()))});
	const auto actual = native->metamagicOverflowReceipt();
	ASSERT_TRUE(actual);
	EXPECT_EQ(actual->recipient, collateral->unitId());
	EXPECT_EQ(actual->secondaryResistancePercent, 0);
	EXPECT_EQ(actual->recipientOwner, PlayerColor(1));

	class Scores final : public heroActionSelection::Observer
	{
	public:
		std::map<size_t, heroActionSelection::CandidateValue> values;
		std::set<size_t> wantedTargets;
		uint32_t target;
		bool failed = false;
		explicit Scores(uint32_t id) : target(id) {}
		void selected(heroActionSelection::SelectionValue) noexcept override {}
		bool observeIdentity() const noexcept override { return true; }
		void candidate(heroActionSelection::CandidateValue value) noexcept override
		{
			try { values.emplace(value.ordinal, value); } catch(...) { failed = true; }
		}
		void candidateIdentity(size_t ordinal, heroActionSelection::CandidateIdentity identity) noexcept override
		{
			try
			{
				if(identity.targets.size() == 1 && identity.targets.front().unitId == target)
					wantedTargets.insert(ordinal);
			}
			catch(...) { failed = true; }
		}
	};
	callback = std::make_shared<QueueCallback>();
	callback->onBattleStarted(battle());
	for(const auto * unit : battle()->battleGetAllStacks(false)) (void)unit->counterAttacks.total();
	const auto before = gameState()->saveToMemory();
	const auto random = wire(*gameHandler->randomizer);
	const auto ambient = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
	battleSearch::Scope full(battleSearch::Profile::Full);
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1, 2);
	evaluator.selectStackAction(active);
	Scores scores(enemy->unitId());
	evaluator.attemptCastingSpell(active, true, &scores);
	ASSERT_FALSE(scores.failed);
	bool scored = false;
	for(const auto ordinal : scores.wantedTargets)
	{
		const auto found = scores.values.find(ordinal);
		if(found != scores.values.end() && found->second.spell == spell)
		{
			EXPECT_TRUE(std::isfinite(found->second.value));
			scored = true;
		}
	}
	EXPECT_TRUE(scored) << "A native post-kill resistance change must not discard the original legal candidate";
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
	EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), ambient);
}
TEST_F(ArcaneOverflowProjectionTest, AuthoredHandOfFateRetainsItsOwnMultiRecipientKernel)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto fate = SpellID(SpellID::decode("new-horizons:handOfFate"));
	ASSERT_TRUE(fate.hasValue());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, fate, "new-horizons:chaosMagic"));
	spells::BattleCast cast(live.get(), attackerSideHero, spells::Mode::HERO, fate.toSpell());
	const auto native = fate.toSpell()->battleMechanics(&cast);
	EXPECT_EQ(newHorizonsMetamagic::overflowSupport(*native, {spells::Destination(enemy)}),
		newHorizonsMetamagic::OverflowSupport::Inapplicable);
}

TEST_F(ArcaneOverflowProjectionTest, ReadyPrimaryRebirthIsUnknownBeforeAnyNativeSeed)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto [type, value] : {std::pair{BonusType::REBIRTH, 100}, std::pair{BonusType::CASTS, 1}})
		effect(enemy, Bonus(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, value, BonusSourceID()));
	ASSERT_TRUE(enemy->acquireState()->canCast());
	spells::BattleCast cast(live.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto native = spell.toSpell()->battleMechanics(&cast);
	const auto before = gameState()->saveToMemory();
	const auto random = wire(*gameHandler->randomizer);
	EXPECT_EQ(newHorizonsMetamagic::overflowSupport(*native, {spells::Destination(enemy)}, true),
		newHorizonsMetamagic::OverflowSupport::Unsupported);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
}
TEST_F(ArcaneOverflowProjectionTest, UnrepresentedDeathEventDoesNotBecomeAZeroUtilityForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Deliberate unknown marker is fixture input; no event is executed.
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::COMBAT_EVENT_TRIGGER, BonusSource::OTHER, 1, BonusSourceID()));
	spells::BattleCast cast(live.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto native = spell.toSpell()->battleMechanics(&cast);
	EXPECT_EQ(newHorizonsMetamagic::overflowSupport(*native, {spells::Destination(enemy)}, true),
		newHorizonsMetamagic::OverflowSupport::Unsupported);
}

TEST_F(ArcaneOverflowProjectionTest, SharedDefenderRerollIsUnknownUntilItsAllowanceIsSpent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 50, BonusSourceID()));
	effect(collateral, Bonus(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 50, BonusSourceID()));
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, {true, false});
	spells::BattleCast cast(live.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto native = spell.toSpell()->battleMechanics(&cast);
	const auto before = gameState()->saveToMemory();
	const auto random = wire(*gameHandler->randomizer);
	EXPECT_EQ(newHorizonsMetamagic::overflowSupport(*native, {spells::Destination(enemy)}, true),
		newHorizonsMetamagic::OverflowSupport::Unsupported);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, {true, true});
	EXPECT_EQ(newHorizonsMetamagic::overflowSupport(*native, {spells::Destination(enemy)}, true),
		newHorizonsMetamagic::OverflowSupport::Supported);
}

}
