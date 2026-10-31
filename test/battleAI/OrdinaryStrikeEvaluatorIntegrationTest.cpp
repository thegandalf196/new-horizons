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

	void prepare(bool volley = false, int mdr = 50)
	{
		ASSERT_NO_FATAL_FAILURE(world());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(volley ? "core:theodorus" : "core:aeris")));
		ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
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
