/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "HeroActionSelectionState.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/HeroDecisionPressure.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/CanonicalOrderValue.h"
#include "../../AI/BattleAI/BattleDamageValue.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/HeroOrderCandidateRules.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/StartInfo.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <array>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace
{
std::string shortlistHex(const std::vector<std::byte> & bytes)
{
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(bytes.size() * 2);
	for(const auto byte : bytes)
	{
		const auto value = std::to_integer<unsigned int>(byte);
		result += digits[value >> 4];
		result += digits[value & 15];
	}
	return result;
}
class ShortlistCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	std::shared_ptr<CPlayerBattleCallback> alternateView;
	std::shared_ptr<CPlayerBattleCallback> getBattle(const BattleID & id) override
	{
		return alternateView ? alternateView : CBattleCallback::getBattle(id);
	}
	ShortlistCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
	void battleMakeUnitAction(const BattleID &, const BattleAction &) override {}
};
class CustomShortlistView final : public CPlayerBattleCallback
{
public:
	using CPlayerBattleCallback::CPlayerBattleCallback;
	mutable size_t policyQueries = 0;
	VisibleHeroPolicy battleGetVisibleHeroPolicy(BattleSide side) const override
	{
		++policyQueries;
		return CPlayerBattleCallback::battleGetVisibleHeroPolicy(side);
	}
};
struct ShortlistTrace final : heroActionSelection::Observer
{
	std::array<heroActionSelection::CandidateValue, 128> values{};
	size_t count = 0;
	heroActionSelection::SelectionValue selection{};
	bool selectedValue = false;
	bool overflow = false;
	void candidate(heroActionSelection::CandidateValue value) noexcept override
	{
		if(count == values.size())
		{
			overflow = true;
			return;
		}
		values[count++] = value;
	}
	void selected(heroActionSelection::SelectionValue value) noexcept override
	{
		selection = value;
		selectedValue = true;
	}
};
std::string fullLedger(const ShortlistTrace & trace)
{
	std::ostringstream out;
	for(size_t i = 0; i < trace.count; ++i)
	{
		const auto & value = trace.values[i];
		out << value.ordinal << ':' << static_cast<int>(value.command) << ':' << value.spell.getNum()
			<< ':' << value.overcharge << ':' << std::bit_cast<uint32_t>(value.value) << ';';
	}
	return out.str();
}
struct ShortlistOutcome
{
	bool performed = false;
	std::vector<BattleAction> actions;
	heroDecisionPressure::Experiment experiment;
	ShortlistTrace trace;
	int64_t nanoseconds = 0;
};
}

TEST(HeroOrderShortlistRankingTest, SupportedThirdCandidateCanWinFullScoringAndIsExplicitPolicyRegret)
{
	const std::vector<heroDecisionPressure::Ranked> cheap{{0, 100}, {1, 99}, {2, 98}};
	const auto refined = heroDecisionPressure::promisingRefinements(cheap);
	EXPECT_EQ(refined, (std::vector<size_t>{0, 1}));
	std::vector<PossibleSpellcast> full(3);
	for(size_t i = 0; i < full.size(); ++i) full[i].command = HeroCommand::FOCUS_FIRE;
	full[0].value = 50;
	full[1].value = 51;
	full[2].value = 100;
	const auto exact = heroActionSelection::choose(full, 0, false, nullptr);
	ASSERT_TRUE(exact);
	EXPECT_EQ(exact->ordinal, 2u);
	full[2].value = std::numeric_limits<float>::lowest();
	const auto bounded = heroActionSelection::choose(full, 0, false, nullptr);
	ASSERT_TRUE(bounded);
	EXPECT_EQ(bounded->ordinal, 1u);
	RecordProperty("ConstructedPolicyRegret", "49");
	// This counterexample forbids an equivalence claim; it does NOT accept any
	// amount of actual gameplay regret. Root reviews observed scenario ledgers.
}

class HeroOrderShortlistConsumerTest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	std::array<CStack *, 3> enemies{};
	std::shared_ptr<ShortlistCallback> callback;
	std::shared_ptr<projectedHitTest::Environment> environment;
	bool neutralArcheryProfile = false;
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(!neutralArcheryProfile)
			return;
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & profiles = heroes["startingSkills"]["startingDevelopmentProfiles"].Struct();
		const auto found = profiles.find("core:orrin");
		ASSERT_NE(found, profiles.end());
		ASSERT_EQ(found->second["startingPerks"].Vector().size(), 1u);
		ASSERT_EQ(found->second["startingPerks"].Vector().front()["perk"].String(), "new-horizons:archery.targetCaller");
		profiles.erase(found); // Supported generic creation fallback, before initHero; never a runtime respec.
		heroes.setOverrideFlag(true); // Keep this complete authored map copy; merging would restore the omitted entry.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
		ASSERT_FALSE(loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)
			["startingSkills"]["startingDevelopmentProfiles"].Struct().contains("core:orrin"));
	}
	void configureWorldInitialization(StartInfo & info, GameRandomizer & randomizer) const override
	{
		info.startTime = 1'700'000'000;
		randomizer.setSeed(1337);
	}
	void prepare(bool current = true, bool overkill = false)
	{
		useRevisedOrders = current;
		dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(1337);
		CRandomGenerator::getDefault().setSeed(1337);
		startGame();
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			hero->removeAllSpells();
			if(hero->hasSpellbook()) hero->removeArtifact(ArtifactPosition::SPELLBOOK);
		}
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 200, ChangeValueMode::ABSOLUTE);
		startBattle();
		BattleUnitsChanged clear;
		clear.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			clear.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(clear);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 5), 200);
		for(size_t i = 0; i < enemies.size(); ++i)
			enemies[i] = addStack(BattleSide::DEFENDER, creatureByName("core:archer"),
				BattleHex(13, 1 + 4 * i), overkill && i == 2 ? 1 : 150 + 50 * i);
		beginCombat();
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = active->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		for(const auto & unit : battle()->stacks) (void)unit->counterAttacks.available();
		callback = std::make_shared<ShortlistCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<projectedHitTest::Environment>(gameState());
		gameHandler->randomizer->setSeed(1337);
		CRandomGenerator::getDefault().setSeed(1337);
	}
	void acquireContactPointBlank(const battle::Units & contacts)
	{
		ASSERT_FALSE(attackerSideHero->getPrimaryGrowthRules()["startingSkills"]["startingDevelopmentProfiles"]
			.Struct().contains("core:orrin"));
		ASSERT_FALSE(attackerSideHero->getPerkState().hasSelection(
			"new-horizons:archery", "new-horizons:archery.targetCaller"));
		const SecondarySkill archery(SecondarySkill::decode("new-horizons:archery"));
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(archery), MasteryLevel::NONE);
		attackerSideHero->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto beforePointBlank = battle()->battleEstimateDamage(
			BattleAttackInfo(active, contacts.front(), 0, true));
		ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(
			"new-horizons:archery", "new-horizons:archery.pointBlankShot", MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(selectPerk("new-horizons:archery", "new-horizons:archery.pointBlankShot", MasteryLevel::BASIC));
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:archery", "new-horizons:archery.pointBlankShot"));
		ASSERT_FALSE(attackerSideHero->getPerkState().hasSelection(
			"new-horizons:archery", "new-horizons:archery.targetCaller"));
		const auto plain = battle()->battleEstimateDamage(BattleAttackInfo(active, contacts.front(), 0, true));
		EXPECT_GE(plain.damage.min, 2 * beforePointBlank.damage.min);
		EXPECT_LE(plain.damage.min, 2 * beforePointBlank.damage.min + 1);
		EXPECT_GE(plain.damage.max, 2 * beforePointBlank.damage.max);
		EXPECT_LE(plain.damage.max, 2 * beforePointBlank.damage.max + 1);
		RecordProperty("PlainAdjacentBeforePointBlank", std::to_string(beforePointBlank.damage.min) + "," + std::to_string(beforePointBlank.damage.max));
		RecordProperty("PlainAdjacentWithPointBlank", std::to_string(plain.damage.min) + "," + std::to_string(plain.damage.max));
		for(const auto * own : battle()->battleAliveUnits(BattleSide::ATTACKER))
			for(const auto * enemy : contacts)
			{
				ASSERT_TRUE(battle()->battleCanShoot(own, enemy->getPosition()));
				ASSERT_TRUE(battle()->isMeleeAttackPossible(enemy, own));
			}
		auto focused = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		PossibleSpellcast candidate;
		candidate.command = HeroCommand::FOCUS_FIRE;
		candidate.commandTargets = {contacts.front()->unitId()};
		const auto admission = heroActionProjection::admitCandidate(*focused, BattleSide::ATTACKER, candidate);
		ASSERT_TRUE(admission.admitted);
		ASSERT_TRUE(admission.orderEffects);
		ASSERT_TRUE(admission.orderAllowance);
		ASSERT_TRUE(focused->projectAcceptedHeroOrder(BattleSide::ATTACKER,
			*admission.orderEffects, *admission.orderAllowance));
		const auto focus = focused->battleEstimateDamage(BattleAttackInfo(
			focused->battleGetUnitByID(active->unitId()), focused->battleGetUnitByID(contacts.front()->unitId()), 0, true));
		RecordProperty("FocusFireAdjacentWithPointBlank", std::to_string(focus.damage.min) + "," + std::to_string(focus.damage.max));
	}
	void preparePositiveRiposteContact()
	{
		neutralArcheryProfile = true;
		ASSERT_NO_FATAL_FAILURE(prepare());
		for(auto * hero : {attackerSideHero, defenderSideHero})
			for(const auto attribute : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE})
				hero->setPrimarySkill(attribute, 0, ChangeValueMode::ABSOLUTE);
		active->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::FREE_SHOOTING, BonusSource::OTHER, 1, BonusSourceID()));
		const BattleHex activePosition = active->getPosition();
		const auto & neighbours = activePosition.getNeighbouringTiles();
		ASSERT_GE(neighbours.size(), enemies.size());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		remove.changedStacks.emplace_back(enemies[1]->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		enemies[1] = nullptr;
		enemies.front()->setPosition(neighbours[0]);
		enemies.back()->setPosition(neighbours[1]);
		const BattleHex firstPosition = enemies.front()->getPosition();
		const BattleHex secondPosition = enemies.back()->getPosition();
		BattleHex allyPosition = BattleHex::INVALID;
		for(const auto hex : firstPosition.getNeighbouringTiles())
			if(hex != activePosition && hex.isAvailable() && BattleHex::getDistance(hex, secondPosition) == 1
				&& !battle()->battleGetUnitByPos(hex, true)
				&& battle()->battleGetAllObstaclesOnPos(hex, false).empty())
			{
				allyPosition = hex;
				break;
			}
		ASSERT_TRUE(allyPosition.isAvailable());
		auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), allyPosition, 200);
		ASSERT_EQ(battle()->battleGetOwner(ally), PlayerColor(0));
		ASSERT_EQ(battle()->battleGetActionController(ally), battle()->battleGetOwner(ally));
		const auto contacts = battle()->battleAliveUnits(BattleSide::DEFENDER);
		ASSERT_EQ(contacts.size(), 2u);
		for(const auto * enemy : contacts)
		{
			ASSERT_EQ(BattleHex::getDistance(activePosition, enemy->getPosition()), 1);
			ASSERT_EQ(BattleHex::getDistance(allyPosition, enemy->getPosition()), 1);
			ASSERT_TRUE(battle()->battleCanShoot(active, enemy->getPosition()));
			ASSERT_FALSE(battle()->battleCanShoot(enemy, activePosition));
			ASSERT_FALSE(battle()->battleCanShoot(enemy, allyPosition));
			ASSERT_FALSE(battle()->battleCanShoot(ally, enemy->getPosition()));
			ASSERT_TRUE(battle()->isMeleeAttackPossible(enemy, active));
			ASSERT_TRUE(battle()->isMeleeAttackPossible(enemy, ally));
			ASSERT_TRUE(battle()->isMeleeAttackPossible(ally, enemy));
		}
		ASSERT_NO_FATAL_FAILURE(acquireContactPointBlank(contacts));
		for(const auto & unit : battle()->stacks)
			(void)unit->counterAttacks.available(); // Same normal preconditioning as prepare(), including the late ally.
	}
	auto snapshot()
	{
		return std::array<std::vector<std::byte>, 5>{gameState()->saveToMemory(),
			projectedHitTest::serialized(*battle()), projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}
	ShortlistOutcome run(bool enabled, bool observe = true, bool passExperiment = true)
	{
		ShortlistOutcome result;
		result.experiment.enableShortlist = enabled;
		callback->submitted.clear();
		const auto started = std::chrono::steady_clock::now();
		{
			int own = 0, opposing = 0;
			for(const auto * unit : battle()->battleGetAllStacks())
				(unit->unitSide() == BattleSide::ATTACKER ? own : opposing)
					+= unit->getCount() * unit->creatureId().toCreature()->getAIValue();
			const float ratio = opposing ? static_cast<float>(own) / opposing : 1.0f;
			const int horizon = gameState()->getStartInfo()->difficulty < 4 ? 2 : 10;
			BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
				BattleSide::ATTACKER, ratio, horizon);
			if(!battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER))
				evaluator.selectStackAction(active);
			result.performed = evaluator.attemptCastingSpell(active, false, observe ? &result.trace : nullptr,
				nullptr, nullptr, nullptr, passExperiment ? &result.experiment : nullptr);
		}
		result.nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - started).count();
		result.actions = callback->submitted;
		return result;
	}
	void selectPerk(const char * skill, const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed, attackerSideHero->getPerkOfferWeightContext());
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.skillId == skill && offers[index].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, index, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No canonical offer for " << perk;
	}
	void expectFullFallback(const ShortlistOutcome & exact, const ShortlistOutcome & policy)
	{
		EXPECT_FALSE(policy.experiment.baselineReady);
		EXPECT_TRUE(policy.experiment.cheapScores.empty());
		EXPECT_TRUE(policy.experiment.refinedOrdinals.empty());
		EXPECT_EQ(policy.experiment.refinementContinuationCalls, 0u);
		EXPECT_EQ(std::count(policy.experiment.declined.begin(), policy.experiment.declined.end(), true), 0);
		EXPECT_EQ(policy.experiment.continuationCalls, exact.experiment.continuationCalls);
		EXPECT_EQ(policy.performed, exact.performed);
		EXPECT_EQ(projectedHitTest::serialized(policy.actions), projectedHitTest::serialized(exact.actions));
		ASSERT_FALSE(exact.trace.overflow);
		ASSERT_FALSE(policy.trace.overflow);
		EXPECT_EQ(fullLedger(policy.trace), fullLedger(exact.trace));
		EXPECT_EQ(policy.trace.selectedValue, exact.trace.selectedValue);
		if(exact.trace.selectedValue)
		{
			EXPECT_EQ(policy.trace.selection.ordinal, exact.trace.selection.ordinal);
			EXPECT_EQ(std::bit_cast<uint32_t>(policy.trace.selection.baseline),
				std::bit_cast<uint32_t>(exact.trace.selection.baseline));
		}
	}
	void assertLegal(const ShortlistOutcome & outcome)
	{
		ASSERT_TRUE(outcome.performed);
		ASSERT_EQ(outcome.actions.size(), 1u);
		const auto & action = outcome.actions.front();
		ASSERT_EQ(action.actionType, EActionType::HERO_COMMAND);
		std::vector<uint32_t> targets;
		for(const auto & target : action.target)
		{
			ASSERT_GE(target.unitValue, 0);
			targets.push_back(static_cast<uint32_t>(target.unitValue));
		}
		EXPECT_TRUE(heroOrderCandidates::commandTargetIsLegal(*callback->getBattle(BattleID(0)),
			BattleSide::ATTACKER, action.command, targets));
	}
	void recordContactRipostePairs(const battle::Units & contacts)
	{
		const auto source = callback->getBattle(BattleID(0));
		auto reference = std::make_shared<HypotheticBattle>(environment.get(), source);
		auto projected = std::make_shared<HypotheticBattle>(environment.get(), source);
		for(const auto * unit : source->battleGetAllUnits(false))
		{
			reference->getForUpdate(unit->unitId());
			projected->getForUpdate(unit->unitId());
		}
		PossibleSpellcast candidate;
		candidate.command = HeroCommand::RIPOSTE;
		const auto admission = heroActionProjection::admitCandidate(*projected, BattleSide::ATTACKER, candidate);
		ASSERT_TRUE(admission.admitted);
		ASSERT_TRUE(admission.orderEffects);
		ASSERT_TRUE(admission.orderAllowance);
		ASSERT_TRUE(projected->projectAcceptedHeroOrder(BattleSide::ATTACKER,
			*admission.orderEffects, *admission.orderAllowance));
		const auto & effects = source->getBattle()->getHeroCommandRules()["commands"]["riposte"]["effects"];
		const auto & order = admission.orderEffects->order;
		ASSERT_TRUE(order.scheduledFor(active->unitId(), projected->battleGetRound()));
		ASSERT_FALSE(order.crownAndAltarAppliesTo(active->unitId()));
		const auto coefficient = [&](const char * key)
		{
			const int intrinsic = heroCommands::coefficient(effects[key], *attackerSideHero,
				order.divineMandateEfficiencyBonusPercent());
			return heroCommands::orderPotency(intrinsic, intrinsic, order, projected->getMagicRules());
		};
		RecordProperty("RiposteNativeReduction", std::to_string(coefficient("meleeDamageReductionPercent")));
		RecordProperty("RiposteNativeCounterBonus", std::to_string(coefficient("retaliationDamagePercent")));
		int own = 0, opposing = 0;
		for(const auto * unit : battle()->battleGetAllStacks())
			(unit->unitSide() == BattleSide::ATTACKER ? own : opposing)
				+= unit->getCount() * unit->creatureId().toCreature()->getAIValue();
		const float ratio = opposing ? static_cast<float>(own) / opposing : 1.0f;
		BattleExchangeEvaluator utility(reference, environment, ratio, 2);
		RecordProperty("ContactStrengthRatio", std::to_string(ratio));
		RecordProperty("ContactStrengthRatioBits", std::to_string(std::bit_cast<uint32_t>(ratio)));
		RecordProperty("ContactNegativeMultiplier", std::to_string(utility.getNegativeEffectMultiplier()));
		RecordProperty("ContactNegativeMultiplierBits", std::to_string(std::bit_cast<uint32_t>(utility.getNegativeEffectMultiplier())));
		RecordProperty("ContactPositiveMultiplier", std::to_string(utility.getPositiveEffectMultiplier()));
		DamageCache original;
		original.buildDamageCache(reference, BattleSide::ATTACKER);
		DamageCache damage(&original);
		ASSERT_EQ(ratio, 1.0f);
		ASSERT_EQ(utility.getNegativeEffectMultiplier(), 1.0f);
		float referenceThreat = 0, riposteThreat = 0;
		std::ostringstream ledger;
		for(const auto * enemy : contacts)
		{
			const auto * attacker = reference->battleGetUnitByID(enemy->unitId());
			const auto * recipient = reference->battleGetUnitByID(active->unitId());
			ledger << enemy->unitId() << ':' << attacker->willMove(0);
			for(const auto & view : {reference, projected})
			{
				DamageEstimation counter;
				const auto incoming = view->battleEstimateDamage(BattleAttackInfo(
					view->battleGetUnitByID(enemy->unitId()), view->battleGetUnitByID(active->unitId()), 0, false), &counter);
				const float incomingValue = AttackPossibility::calculateDamageReduce(attacker, recipient,
					static_cast<uint64_t>(battleDamageValue::averageOrderDamage(incoming)), damage, reference);
				const float counterValue = AttackPossibility::calculateDamageReduce(recipient, attacker,
					static_cast<uint64_t>(battleDamageValue::averageOrderDamage(counter)), damage, reference);
				const float net = incomingValue * utility.getNegativeEffectMultiplier()
					- counterValue * utility.getPositiveEffectMultiplier();
				(view == reference ? referenceThreat : riposteThreat) += std::max(0.0f, net);
				ledger << ':' << incoming.damage.min << ',' << incoming.damage.max
					<< ':' << counter.damage.min << ',' << counter.damage.max
					<< ':' << incomingValue << ',' << counterValue << ',' << net << ',' << std::max(0.0f, net)
					<< ':' << std::bit_cast<uint32_t>(incomingValue) << ',' << std::bit_cast<uint32_t>(counterValue)
					<< ',' << std::bit_cast<uint32_t>(net) << ',' << std::bit_cast<uint32_t>(std::max(0.0f, net));
			}
			ledger << ';';
		}
		RecordProperty("ContactNativePairLedger", ledger.str());
		RecordProperty("ContactReferenceThreat", std::to_string(referenceThreat));
		RecordProperty("ContactRiposteThreat", std::to_string(riposteThreat));
		EXPECT_GT(referenceThreat, riposteThreat);
		EXPECT_GT(riposteThreat, 0.0f);
	}

};

TEST_F(HeroOrderShortlistConsumerTest, PositiveShortlistSkipsActualContinuationsAndKeepsRefinedFullScores)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = snapshot();
	const auto exact = run(false);
	EXPECT_EQ(snapshot(), before);
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	ASSERT_TRUE(policy.experiment.baselineReady);
	ASSERT_GE(policy.experiment.cheapScores.size(), 3u);
	ASSERT_FALSE(policy.experiment.refinedOrdinals.empty());
	ASSERT_LE(policy.experiment.refinedOrdinals.size(), 2u);
	ASSERT_GT(std::count(policy.experiment.declined.begin(), policy.experiment.declined.end(), true), 0);
	ASSERT_GT(policy.experiment.refinementContinuationCalls, 0u);
	ASSERT_LE(policy.experiment.refinementContinuationCalls, 2u);
	EXPECT_LT(policy.experiment.continuationCalls, exact.experiment.continuationCalls);
	ASSERT_FALSE(exact.trace.overflow);
	ASSERT_FALSE(policy.trace.overflow);
	ASSERT_EQ(policy.trace.count, exact.trace.count);
	for(size_t i = 0; i < exact.trace.count; ++i)
		if(!policy.experiment.declined[i])
		{
			EXPECT_EQ(std::bit_cast<uint32_t>(policy.trace.values[i].value),
				std::bit_cast<uint32_t>(exact.trace.values[i].value));
		}
	bool uselessRiposte = false;
	for(const auto & estimate : policy.experiment.cheapScores)
	{
		const auto command = exact.trace.values[estimate.ordinal].command;
		EXPECT_NE(command, HeroCommand::FEINT);
		EXPECT_NE(command, HeroCommand::VANGUARD);
		if(command == HeroCommand::RIPOSTE)
		{
			uselessRiposte = true;
			EXPECT_EQ(estimate.marginal, 0.0f);
			EXPECT_TRUE(policy.experiment.declined[estimate.ordinal]);
		}
	}
	EXPECT_TRUE(uselessRiposte); // Ranged-only opportunities cannot earn a melee/retaliation buff.
	ASSERT_NO_FATAL_FAILURE(assertLegal(exact));
	ASSERT_NO_FATAL_FAILURE(assertLegal(policy));
	ASSERT_TRUE(exact.trace.selectedValue);
	ASSERT_TRUE(policy.trace.selectedValue);
	const auto oldWinner = exact.trace.selection.ordinal;
	const auto chosen = policy.trace.selection.ordinal;
	EXPECT_FALSE(policy.experiment.declined[chosen]);
	RecordProperty("OriginalFullScoreLedger", fullLedger(exact.trace));
	RecordProperty("PolicyFullScoreLedger", fullLedger(policy.trace));
	RecordProperty("OriginalSelectedOrdinal", std::to_string(oldWinner));
	RecordProperty("PolicySelectedOrdinal", std::to_string(chosen));
	RecordProperty("FullWinnerWasPolicyDeclined", policy.experiment.declined[oldWinner] ? "true" : "false");
	RecordProperty("PolicyRegretInOriginalFullScore", std::to_string(
		exact.trace.values[oldWinner].value - exact.trace.values[chosen].value));
	RecordProperty("OriginalContinuationCalls", std::to_string(exact.experiment.continuationCalls));
	RecordProperty("PolicyContinuationCalls", std::to_string(policy.experiment.continuationCalls));
	RecordProperty("WholeOriginalDecisionNanoseconds", std::to_string(exact.nanoseconds));
	RecordProperty("WholePolicyDecisionNanoseconds", std::to_string(policy.nanoseconds));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), policy.actions.front()));
}

TEST_F(HeroOrderShortlistConsumerTest, AdjacentMeleeOpportunitiesRefinePositiveRiposteWithoutExtraContinuations)
{
	ASSERT_NO_FATAL_FAILURE(preparePositiveRiposteContact());
	const auto contacts = battle()->battleAliveUnits(BattleSide::DEFENDER);
	const auto before = snapshot();
	ASSERT_NO_FATAL_FAILURE(recordContactRipostePairs(contacts));
	EXPECT_EQ(snapshot(), before);
	const auto exact = run(false);
	EXPECT_EQ(snapshot(), before);
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	ASSERT_TRUE(policy.experiment.baselineReady);
	ASSERT_GE(policy.experiment.cheapScores.size(), 3u);
	ASSERT_GT(policy.experiment.refinementContinuationCalls, 0u);
	ASSERT_LE(policy.experiment.refinementContinuationCalls, 2u);
	EXPECT_LT(policy.experiment.continuationCalls, exact.experiment.continuationCalls);
	ASSERT_GT(std::count(policy.experiment.declined.begin(), policy.experiment.declined.end(), true), 0);
	ASSERT_FALSE(exact.trace.overflow);
	ASSERT_FALSE(policy.trace.overflow);
	ASSERT_EQ(policy.trace.count, exact.trace.count);
	std::ostringstream cheapLedger;
	for(const auto & estimate : policy.experiment.cheapScores)
		cheapLedger << estimate.ordinal << ':' << static_cast<int>(exact.trace.values[estimate.ordinal].command)
			<< ':' << std::bit_cast<uint32_t>(estimate.marginal) << ';';
	RecordProperty("ContactCheapOrderLedger", cheapLedger.str());
	bool positiveRiposte = false;
	for(const auto & estimate : policy.experiment.cheapScores)
		if(exact.trace.values[estimate.ordinal].command == HeroCommand::RIPOSTE)
		{
			positiveRiposte = true;
			RecordProperty("ActualRiposteScoutMarginal", std::to_string(estimate.marginal));
			EXPECT_GT(estimate.marginal, 0.0f);
			EXPECT_FALSE(policy.experiment.declined[estimate.ordinal]);
			EXPECT_NE(std::find(policy.experiment.refinedOrdinals.begin(),
				policy.experiment.refinedOrdinals.end(), estimate.ordinal), policy.experiment.refinedOrdinals.end());
		}
	EXPECT_TRUE(positiveRiposte);
	for(size_t index = 0; index < exact.trace.count; ++index)
		if(!policy.experiment.declined[index])
		{
			EXPECT_EQ(std::bit_cast<uint32_t>(policy.trace.values[index].value),
				std::bit_cast<uint32_t>(exact.trace.values[index].value));
		}
	ASSERT_NO_FATAL_FAILURE(assertLegal(exact));
	ASSERT_NO_FATAL_FAILURE(assertLegal(policy));
	ASSERT_TRUE(exact.trace.selectedValue);
	ASSERT_TRUE(policy.trace.selectedValue);
	RecordProperty("OriginalFullScoreLedger", fullLedger(exact.trace));
	RecordProperty("PolicyFullScoreLedger", fullLedger(policy.trace));
	RecordProperty("PolicyRegretInOriginalFullScore", std::to_string(
		exact.trace.values[exact.trace.selection.ordinal].value - exact.trace.values[policy.trace.selection.ordinal].value));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), policy.actions.front()));
}

TEST_F(HeroOrderShortlistConsumerTest, OverkillTargetGetsNoFreeFocusFireUtility)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	auto baseline = heroDecisionPressure::Baseline::create(environment.get(), callback->getBattle(BattleID(0)), BattleSide::ATTACKER);
	ASSERT_TRUE(baseline);
	PossibleSpellcast candidate;
	candidate.command = HeroCommand::FOCUS_FIRE;
	candidate.commandTargets = {enemies.back()->unitId()};
	auto state = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto admission = heroActionProjection::admitCandidate(*state, BattleSide::ATTACKER, candidate);
	ASSERT_TRUE(admission.admitted);
	ASSERT_TRUE(admission.orderEffects);
	ASSERT_TRUE(admission.orderAllowance);
	ASSERT_TRUE(state->projectAcceptedHeroOrder(BattleSide::ATTACKER, *admission.orderEffects, *admission.orderAllowance));
	const auto value = baseline->orderUtility(state, HeroCommand::FOCUS_FIRE);
	ASSERT_TRUE(value);
	EXPECT_EQ(*value, 0.0f);
	const auto before = snapshot();
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	ASSERT_TRUE(policy.experiment.baselineReady);
	EXPECT_GT(std::count(policy.experiment.declined.begin(), policy.experiment.declined.end(), true), 0);
	ASSERT_NO_FATAL_FAILURE(assertLegal(policy));
	if(policy.actions.front().command == HeroCommand::FOCUS_FIRE)
	{
		EXPECT_NE(policy.actions.front().target.front().unitValue, enemies.back()->unitId());
	}
}

TEST_F(HeroOrderShortlistConsumerTest, SpentHeroAllowanceCannotBeReopenedByShortlisting)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	const auto before = snapshot();
	const auto policy = run(true);
	EXPECT_FALSE(policy.performed);
	EXPECT_TRUE(policy.actions.empty());
	EXPECT_EQ(policy.experiment.baselineAttempts, 0u);
	EXPECT_EQ(snapshot(), before);
}

TEST_F(HeroOrderShortlistConsumerTest, HistoricalCaptureRemainsOnlyOriginalScoresAndChoices)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto before = snapshot();
	const auto exact = run(false);
	EXPECT_EQ(snapshot(), before);
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	EXPECT_FALSE(policy.experiment.baselineReady);
	EXPECT_TRUE(policy.experiment.cheapScores.empty());
	EXPECT_EQ(policy.experiment.continuationCalls, exact.experiment.continuationCalls);
	EXPECT_EQ(projectedHitTest::serialized(policy.actions), projectedHitTest::serialized(exact.actions));
	ASSERT_EQ(policy.trace.count, exact.trace.count);
	for(size_t i = 0; i < exact.trace.count; ++i)
		EXPECT_EQ(std::bit_cast<uint32_t>(policy.trace.values[i].value),
			std::bit_cast<uint32_t>(exact.trace.values[i].value));
}

TEST_F(HeroOrderShortlistConsumerTest, NullDefaultAndDisabledExperimentKeepAllFullScoresAndActions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = snapshot();
	const auto ordinary = run(false, true, false);
	EXPECT_EQ(snapshot(), before);
	const auto diagnostic = run(false);
	EXPECT_EQ(snapshot(), before);
	EXPECT_TRUE(diagnostic.experiment.cheapScores.empty());
	EXPECT_EQ(diagnostic.experiment.baselineAttempts, 0u);
	EXPECT_EQ(projectedHitTest::serialized(ordinary.actions), projectedHitTest::serialized(diagnostic.actions));
	ASSERT_EQ(ordinary.trace.count, diagnostic.trace.count);
	for(size_t index = 0; index < ordinary.trace.count; ++index)
		EXPECT_EQ(std::bit_cast<uint32_t>(ordinary.trace.values[index].value),
			std::bit_cast<uint32_t>(diagnostic.trace.values[index].value));
}

TEST_F(HeroOrderShortlistConsumerTest, UncertainMeleeTopologyKeepsOriginalPathAndHoldAnchorSemantics)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemies.front()->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::FORGETFULL, BonusSource::OTHER, 100, BonusSourceID()));
	ASSERT_FALSE(battle()->battleCanShoot(enemies.front(), active->getPosition()));
	const auto before = snapshot();
	const auto exact = run(false);
	EXPECT_EQ(snapshot(), before);
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	EXPECT_FALSE(policy.experiment.baselineReady);
	EXPECT_TRUE(policy.experiment.cheapScores.empty());
	EXPECT_EQ(policy.experiment.continuationCalls, exact.experiment.continuationCalls);
	EXPECT_EQ(projectedHitTest::serialized(policy.actions), projectedHitTest::serialized(exact.actions));
	ASSERT_EQ(policy.trace.count, exact.trace.count);
	for(size_t index = 0; index < exact.trace.count; ++index)
		EXPECT_EQ(std::bit_cast<uint32_t>(policy.trace.values[index].value),
			std::bit_cast<uint32_t>(exact.trace.values[index].value));
}

TEST_F(HeroOrderShortlistConsumerTest, ExtractedOrderCompletionKeepsNativeCommitAndContinuationFields)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = snapshot();
	for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		HeroCommand::FOCUS_FIRE, HeroCommand::RIPOSTE, HeroCommand::BRACE, HeroCommand::FEINT, HeroCommand::VANGUARD})
	{
		PossibleSpellcast candidate;
		candidate.command = command;
		if(command == HeroCommand::FOCUS_FIRE) candidate.commandTargets = {enemies.front()->unitId()};
		const auto source = callback->getBattle(BattleID(0));
		auto expected = std::make_shared<HypotheticBattle>(environment.get(), source);
		auto actual = std::make_shared<HypotheticBattle>(environment.get(), source);
		const auto expectedAdmission = heroActionProjection::admitCandidate(*expected, BattleSide::ATTACKER, candidate);
		const auto actualAdmission = heroActionProjection::admitCandidate(*actual, BattleSide::ATTACKER, candidate);
		ASSERT_TRUE(expectedAdmission.admitted);
		ASSERT_TRUE(actualAdmission.admitted);
		ASSERT_TRUE(expectedAdmission.orderEffects);
		ASSERT_TRUE(expectedAdmission.orderAllowance);
		ASSERT_TRUE(expected->projectAcceptedHeroOrder(BattleSide::ATTACKER,
			*expectedAdmission.orderEffects, *expectedAdmission.orderAllowance));
		std::optional<HypotheticBattle::ProjectedOrderContinuation> continuation;
		if(!heroOrderValue::usesRevisedArmyHeuristic(*source, command))
			continuation = expected->projectOrderContinuation(BattleSide::ATTACKER,
				*expectedAdmission.orderAllowance, *expectedAdmission.orderEffects);
		const auto result = heroActionProjection::completeOrder(*actual, *source,
			BattleSide::ATTACKER, candidate, actualAdmission);
		ASSERT_TRUE(result.accepted);
		ASSERT_EQ(result.continuation.has_value(), continuation.has_value());
		if(continuation)
		{
			EXPECT_EQ(result.continuation->kind, continuation->kind);
			EXPECT_EQ(result.continuation->actor, continuation->actor);
			EXPECT_EQ(result.continuation->futureHeroChoicesUnvalued, continuation->futureHeroChoicesUnvalued);
			EXPECT_EQ(result.continuation->orderRandomOutcomesUnvalued, continuation->orderRandomOutcomesUnvalued);
			EXPECT_EQ(result.continuation->activationEffectsUnvalued, continuation->activationEffectsUnvalued);
		}
		EXPECT_EQ(heroSelectionTest::snapshot(*actual, *gameHandler->randomizer, CRandomGenerator::getDefault()),
			heroSelectionTest::snapshot(*expected, *gameHandler->randomizer, CRandomGenerator::getDefault()));
	}
	EXPECT_EQ(snapshot(), before);
}

TEST_F(HeroOrderShortlistConsumerTest, AuthoritativeDoubleCommandPendingBypassesScoutAndKeepsFullDecision)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectPerk("new-horizons:command", "new-horizons:command.aggressiveCommander", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk("new-horizons:command", "new-horizons:command.veteranCommander", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(selectPerk("new-horizons:command", "new-horizons:command.doubleCommand", MasteryLevel::EXPERT));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto before = snapshot();
	const auto exact = run(false);
	EXPECT_EQ(snapshot(), before);
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	EXPECT_EQ(policy.experiment.baselineAttempts, 0u);
	ASSERT_NO_FATAL_FAILURE(expectFullFallback(exact, policy));
	ASSERT_NO_FATAL_FAILURE(assertLegal(policy));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), policy.actions.front()));
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
}

TEST_F(HeroOrderShortlistConsumerTest, SharedPurposeCompanionRetainsFullForecastInsteadOfZeroEstimate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectPerk("new-horizons:divineMandate", "new-horizons:divineMandate.sharedPurpose", MasteryLevel::BASIC));
	ASSERT_TRUE(newHorizonsDivineMandate::hasSharedPurposePerk(attackerSideHero));
	const auto before = snapshot();
	const auto exact = run(false);
	EXPECT_EQ(snapshot(), before);
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	EXPECT_EQ(policy.experiment.baselineAttempts, 1u);
	ASSERT_NO_FATAL_FAILURE(expectFullFallback(exact, policy));
	ASSERT_NO_FATAL_FAILURE(assertLegal(policy));
}

TEST_F(HeroOrderShortlistConsumerTest, CustomVisiblePolicyGetterKeepsItsFullQueriesScoresAndActions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto custom = std::make_shared<CustomShortlistView>(battle(), PlayerColor(0));
	callback->alternateView = custom;
	const auto before = snapshot();
	const auto exact = run(false);
	const auto queries = custom->policyQueries;
	EXPECT_EQ(snapshot(), before);
	custom->policyQueries = 0;
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	EXPECT_EQ(custom->policyQueries, queries);
	EXPECT_GT(queries, 0u);
	EXPECT_EQ(policy.experiment.baselineAttempts, 1u);
	ASSERT_NO_FATAL_FAILURE(expectFullFallback(exact, policy));
}

TEST_F(HeroOrderShortlistConsumerTest, AdditionalAttackTopologyKeepsTheOriginalFullForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	active->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	const auto before = snapshot();
	const auto exact = run(false);
	EXPECT_EQ(snapshot(), before);
	const auto policy = run(true);
	EXPECT_EQ(snapshot(), before);
	EXPECT_EQ(policy.experiment.baselineAttempts, 1u);
	ASSERT_NO_FATAL_FAILURE(expectFullFallback(exact, policy));
	ASSERT_NO_FATAL_FAILURE(assertLegal(policy));
}

TEST_F(HeroOrderShortlistConsumerTest, HiddenCruelVolleyPolicyCannotChangeThePositiveVisibleShortlist)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:jabarkas")));
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * enemy : enemies)
		remove.changedStacks.emplace_back(enemy->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	for(size_t index = 0; index < enemies.size(); ++index)
		enemies[index] = addStack(BattleSide::DEFENDER, creatureByName("core:orc"),
			BattleHex(13, 1 + 4 * index), 150 + 50 * index);
	int64_t casualty = active->getMaxHealth();
	active->damage(casualty);
	const auto source = callback->getBattle(BattleID(0));
	ASSERT_EQ(source->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	const BattleAttackInfo attack(enemies.front(), active, 0, true);
	auto & rules = const_cast<JsonNode &>(defenderSideHero->getPrimaryGrowthRules());
	auto & coefficient = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]["cruelVolleyDamagePercent"];
	const auto visibleStats = [this]
	{
		std::vector<std::array<int64_t, 7>> result;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			result.push_back({unit->unitId(), unit->getPosition().toInt(), unit->getCount(),
				unit->getMaxHealth(), unit->getAvailableHealth(), unit->getAttack(false), unit->getDefense(false)});
		return result;
	};
	const auto visibleBefore = visibleStats();
	std::optional<ShortlistOutcome> previous, previousExact;
	for(const int percent : {0, 60})
	{
		coefficient.Integer() = percent;
		EXPECT_EQ(visibleStats(), visibleBefore);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100 + percent);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*source, attack), 100);
		const auto before = snapshot();
		const auto exact = run(false);
		EXPECT_EQ(snapshot(), before);
		const auto policy = run(true);
		EXPECT_EQ(snapshot(), before);
		ASSERT_FALSE(exact.trace.overflow);
		ASSERT_FALSE(policy.trace.overflow);
		ASSERT_EQ(policy.trace.count, exact.trace.count);
		ASSERT_TRUE(policy.experiment.baselineReady);
		ASSERT_GE(policy.experiment.cheapScores.size(), 3u);
		ASSERT_GT(policy.experiment.refinementContinuationCalls, 0u);
		ASSERT_LE(policy.experiment.refinementContinuationCalls, 2u);
		ASSERT_NO_FATAL_FAILURE(assertLegal(policy));
		for(size_t index = 0; index < exact.trace.count; ++index)
			if(!policy.experiment.declined[index])
			{
				EXPECT_EQ(std::bit_cast<uint32_t>(policy.trace.values[index].value),
					std::bit_cast<uint32_t>(exact.trace.values[index].value));
			}
		if(previous)
		{
			EXPECT_EQ(fullLedger(exact.trace), fullLedger(previousExact->trace));
			EXPECT_EQ(projectedHitTest::serialized(exact.actions), projectedHitTest::serialized(previousExact->actions));
			EXPECT_EQ(fullLedger(policy.trace), fullLedger(previous->trace));
			EXPECT_EQ(projectedHitTest::serialized(policy.actions), projectedHitTest::serialized(previous->actions));
			EXPECT_EQ(policy.experiment.refinedOrdinals, previous->experiment.refinedOrdinals);
			EXPECT_EQ(policy.experiment.declined, previous->experiment.declined);
			ASSERT_EQ(policy.experiment.cheapScores.size(), previous->experiment.cheapScores.size());
			for(size_t index = 0; index < policy.experiment.cheapScores.size(); ++index)
			{
				EXPECT_EQ(policy.experiment.cheapScores[index].ordinal, previous->experiment.cheapScores[index].ordinal);
				EXPECT_EQ(std::bit_cast<uint32_t>(policy.experiment.cheapScores[index].marginal),
					std::bit_cast<uint32_t>(previous->experiment.cheapScores[index].marginal));
			}
		}
		previous = policy;
		previousExact = exact;
	}
}

TEST_F(HeroOrderShortlistConsumerTest, DISABLED_CurrentRangedWholeDecisionWall)
{
	const char * mode = std::getenv("NH_TEST_ORDER_SHORTLIST_POLICY");
	ASSERT_TRUE(mode && (std::string_view(mode) == "0" || std::string_view(mode) == "1"));
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = snapshot();
	const auto outcome = run(std::string_view(mode) == "1", false);
	EXPECT_EQ(snapshot(), before);
	if(std::string_view(mode) == "1")
	{
		ASSERT_TRUE(outcome.experiment.baselineReady);
		ASSERT_GT(outcome.experiment.refinementContinuationCalls, 0u);
		ASSERT_LE(outcome.experiment.refinementContinuationCalls, 2u);
		ASSERT_GE(outcome.experiment.cheapScores.size(), 3u);
		ASSERT_GT(std::count(outcome.experiment.declined.begin(), outcome.experiment.declined.end(), true), 0);
	}
	else
	{
		EXPECT_FALSE(outcome.experiment.baselineReady);
		EXPECT_TRUE(outcome.experiment.cheapScores.empty());
	}
	RecordProperty("WholeDecisionNanoseconds", std::to_string(outcome.nanoseconds));
	RecordProperty("ActionsWire", shortlistHex(projectedHitTest::serialized(outcome.actions)));
	RecordProperty("StateAndAllRngWire", shortlistHex(projectedHitTest::serialized(before)));
	RecordProperty("Performed", outcome.performed ? "true" : "false");
	RecordProperty("ExperimentMode", mode);
	RecordProperty("ContinuationCalls", std::to_string(outcome.experiment.continuationCalls));
	RecordProperty("RefinementContinuationCalls", std::to_string(outcome.experiment.refinementContinuationCalls));
	RecordProperty("CheapCandidates", std::to_string(outcome.experiment.cheapScores.size()));
	RecordProperty("DeclinedCandidates", std::to_string(std::count(outcome.experiment.declined.begin(), outcome.experiment.declined.end(), true)));
}
