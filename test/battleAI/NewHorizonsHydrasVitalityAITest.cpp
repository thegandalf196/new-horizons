/*
 * NewHorizonsHydrasVitalityAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto hydrasVitalityKey = "new-horizons:hydrasVitality";

SpellID hydrasVitalitySpell()
{
	return SpellID(SpellID::decode(std::string(hydrasVitalityKey)));
}

class HydrasVitalityEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit HydrasVitalityEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class HydrasVitalityCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	HydrasVitalityCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsHydrasVitalityAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * target = nullptr;
	CStack * nonliving = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<HydrasVitalityEnvironment> environment;
	std::shared_ptr<HydrasVitalityCallback> callback;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	bool advanceUntilNextActivation(const CStack * stack)
	{
		for(int attempt = 0; attempt < 32; ++attempt)
		{
			const auto * activeStack = battle()->battleActiveUnit();
			if(!activeStack)
				return false;
			if(activeStack == stack)
				return true;

			const auto player = battle()->sideToPlayer(activeStack->unitSide());
			const auto action = BattleAction::makeDefend(activeStack);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action))
				return false;
			if(battle()->battleActiveUnit() == stack)
				return true;
		}
		return false;
	}

	void prepare(const bool onlyInvalidTargets = false)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = hydrasVitalitySpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const auto natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
		ASSERT_GE(natureMagicId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		// Keep the active creature outside Hydra's biological target rules. The
		// spell should offer only the additional capacity reserve to a friendly
		// organic stack, and the opposing side must remain present during AI evaluation.
		// Keep one distant enemy in the battle, but prevent it from advancing.
		// This preserves legal Order candidates while removing Brace's genuine
		// preemptive opportunity from this controlled Hydra-selection fixture.
		active = addStack(BattleSide::ATTACKER,
			creatureByName(onlyInvalidTargets ? "core:skeleton" : "core:stoneGolem"),
			BattleHex(3, 5), 1);
		if(onlyInvalidTargets)
			nonliving = addStack(BattleSide::ATTACKER,
				creatureByName("core:stoneGolem"), BattleHex(4, 5), 1);
		else
			target = addStack(BattleSide::ATTACKER,
				creatureByName("core:pikeman"), BattleHex(4, 5), 50);
		enemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:peasant"), BattleHex(14, 5), 1);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		if(onlyInvalidTargets)
			ASSERT_NE(nonliving, nullptr);
		else
			ASSERT_NE(target, nullptr);

		const auto immobilize = [](CStack * stack)
		{
			Bonus bonus;
			bonus.type = BonusType::STACKS_SPEED;
			bonus.duration = BonusDuration::ONE_BATTLE;
			bonus.val = -stack->getMovementRange();
			stack->addNewBonus(std::make_shared<Bonus>(bonus));
		};
		immobilize(active);
		if(target)
			immobilize(target);
		if(nonliving)
			immobilize(nonliving);
		immobilize(enemy);

		beginCombat();
		ASSERT_TRUE(advanceUntilNextActivation(active));

		callback = std::make_shared<HydrasVitalityCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<HydrasVitalityEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsHydrasVitalityAITest, SubmitsOnlyLegalFriendlyCastAndMatchesDetachedActivationForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = hydrasVitalitySpell();
	const auto * hydrasVitality = spell.toSpell();
	ASSERT_NE(hydrasVitality, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(target->alive());
	ASSERT_TRUE(enemy->alive());

	spells::BattleCast targetPreview(battle(), attackerSideHero, spells::Mode::HERO, hydrasVitality);
	const auto targetMechanics = hydrasVitality->battleMechanics(&targetPreview);
	ASSERT_EQ(targetMechanics->getTargetTypes(), (std::vector<spells::AimType>{spells::AimType::CREATURE}));
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(targetMechanics.get());
	ASSERT_EQ(viableTargets.size(), 1u);
	ASSERT_EQ(viableTargets.front().size(), 1u);
	ASSERT_EQ(viableTargets.front().front().unitValue->unitId(), target->unitId());
	EXPECT_FALSE(targetMechanics->canBeCastAt({spells::Destination(active)}));
	EXPECT_FALSE(targetMechanics->canBeCastAt({spells::Destination(enemy)}));

	const auto targetCountBefore = target->getCount();
	const auto targetHealthBefore = target->getAvailableHealth();
	const auto targetMaximumBefore = target->getMaxHealth();
	const auto enemyHealthBefore = enemy->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, spell);
	const auto submittedTarget = action.getTarget(battle());
	ASSERT_EQ(submittedTarget.size(), 1u);
	ASSERT_EQ(submittedTarget.front().unitValue->unitId(), target->unitId());

	// Apply the submitted aim to a detached battle, then project the genuine
	// activation-start path. Neither forecast step may mutate the live battle.
	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	const auto * projectedTarget = projected.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	spells::Target projectedAim{spells::Destination(projectedTarget)};
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, hydrasVitality);
	const auto projectedMechanics = hydrasVitality->battleMechanics(&projectedCast);
	ASSERT_TRUE(projectedMechanics->canBeCastAt(projectedAim));
	projectedMechanics->castEval(projected.getServerCallback(), projectedAim);
	const auto * projectedAfterCast = projected.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedAfterCast, nullptr);
	EXPECT_EQ(projectedAfterCast->getCount(), targetCountBefore);
	EXPECT_GT(projectedAfterCast->getMaxHealth(), targetMaximumBefore);
	EXPECT_EQ(projectedAfterCast->getAvailableHealth(), targetHealthBefore)
		<< "Enhanced capacity must not be counted as cast-time healing";
	const auto projectedHealthAfterCast = projectedAfterCast->getAvailableHealth();
	const auto * projectedHealthState = dynamic_cast<const battle::CUnitState *>(projectedAfterCast);
	ASSERT_NE(projectedHealthState, nullptr);
	const auto expectedActivationHeal = projectedHealthState->capacityRegenerationProjectedHeal();
	ASSERT_GT(expectedActivationHeal, 0);
	projected.nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto * projectedAfterActivation = projected.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedAfterActivation, nullptr);
	EXPECT_EQ(projectedAfterActivation->getAvailableHealth() - projectedHealthAfterCast, expectedActivationHeal)
		<< "AI forecasting should consume exactly the shared Hydra regeneration at a genuine activation";
	EXPECT_EQ(target->getAvailableHealth(), targetHealthBefore);
	EXPECT_EQ(target->getMaxHealth(), targetMaximumBefore);
	EXPECT_EQ(target->getCount(), targetCountBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getMaxHealth(), projectedAfterCast->getMaxHealth());
	EXPECT_EQ(target->getCount(), targetCountBefore);
	EXPECT_EQ(target->getAvailableHealth(), targetHealthBefore)
		<< "Authoritative cast must preserve the existing body HP while increasing capacity";
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);

	ASSERT_TRUE(advanceUntilNextActivation(target));
	EXPECT_EQ(target->getAvailableHealth(), projectedAfterActivation->getAvailableHealth())
		<< "The real activation and detached forecast should restore the same Hydra HP";
}

TEST_F(NewHorizonsHydrasVitalityAITest, RejectsUndeadAndNonlivingTargetsWithoutSubmittingHydrasVitality)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto spell = hydrasVitalitySpell();
	const auto * hydrasVitality = spell.toSpell();
	ASSERT_NE(hydrasVitality, nullptr);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(nonliving, nullptr);

	spells::BattleCast targetPreview(battle(), attackerSideHero, spells::Mode::HERO, hydrasVitality);
	const auto targetMechanics = hydrasVitality->battleMechanics(&targetPreview);
	EXPECT_FALSE(targetMechanics->canBeCastAt({spells::Destination(active)}));
	EXPECT_FALSE(targetMechanics->canBeCastAt({spells::Destination(nonliving)}));
	EXPECT_FALSE(targetMechanics->canBeCastAt({spells::Destination(enemy)}));
	EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(targetMechanics.get()).empty());

	const auto manaBefore = attackerSideHero->getManaAvailable();
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	EXPECT_TRUE(evaluator.canCastSpell());
	evaluator.attemptCastingSpell(active);

	EXPECT_TRUE(std::none_of(callback->submitted.begin(), callback->submitted.end(), [&](const BattleAction & action)
	{
		return action.actionType == EActionType::HERO_SPELL && action.spell == spell;
	})) << "A no-target Hydra spell must not be submitted; any legal Order remains eligible";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_FALSE(active->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))));
	EXPECT_FALSE(nonliving->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))));
}
