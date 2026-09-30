/*
 * NewHorizonsChaosBlinkAITest.cpp, part of VCMI engine
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
#include "../../lib/CRandomGenerator.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsBlink.h"

namespace
{
SpellID chaosBlinkSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsBlink::SPELL_ID)));
}

class ChaosBlinkEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ChaosBlinkEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ChaosBlinkCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	ChaosBlinkCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

struct RandomStateArchive
{
	bool saving = true;
	std::string state;

	void operator&(std::string & value)
	{
		state = value;
	}
};

std::string randomState(CRandomGenerator & generator)
{
	RandomStateArchive archive;
	generator.serialize(archive);
	return archive.state;
}

struct StackSnapshot
{
	uint32_t id = 0;
	BattleHex position;
	int32_t count = 0;
	int64_t health = 0;
};
}

class NewHorizonsChaosBlinkAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	std::shared_ptr<ChaosBlinkEnvironment> environment;
	std::shared_ptr<ChaosBlinkCallback> callback;

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
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), player,
				BattleAction::makeDefend(activeStack)))
				return false;
			if(battle()->battleActiveUnit() == stack)
				return true;
		}
		return false;
	}

	void prepare(bool blinkmaster)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = chaosBlinkSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		// Keep generic Order values representative of a low-combat Wizard profile
		// instead of inheriting unspecified map-fixture hero growth. Blink still
		// exercises its wider endpoint distribution with explicit SP/Knowledge.
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 5, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 5, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const auto chaosMagicId = SecondarySkill::decode(std::string(newHorizonsBlink::CHAOS_SKILL));
		ASSERT_GE(chaosMagicId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(chaosMagicId), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		if(blinkmaster)
		{
			attackerSideHero->applyPerkSelection({std::string(newHorizonsBlink::CHAOS_SKILL),
				std::string(newHorizonsBlink::BLINKMASTER_PERK)});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsBlink::CHAOS_SKILL,
				newHorizonsBlink::BLINKMASTER_PERK));
		}
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		// Keep legal Orders available, but make the tactical Blink opportunity
		// concrete: the Grand Elves start adjacent to a dangerous ArchDevil stack,
		// so they cannot shoot and are exposed to its melee attack. A legal Blink
		// landing can break contact and restore their ranged attack. Immobilization
		// prevents a movement-triggered Brace opportunity without disabling either
		// melee attacks at contact or shooting after relocation.
		active = addStack(BattleSide::ATTACKER,
			creatureByName("core:stoneGolem"), BattleHex(2, 8), 1);
		auto * target = addStack(BattleSide::ATTACKER,
			creatureByName("core:grandElf"), BattleHex(4, 5), 80);
		CStack * enemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:archDevil"), BattleHex(5, 5), 5);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(enemy, nullptr);

		const auto immobilize = [](CStack * stack)
		{
			Bonus bonus;
			bonus.type = BonusType::STACKS_SPEED;
			bonus.duration = BonusDuration::ONE_BATTLE;
			bonus.val = -static_cast<int32_t>(stack->getMovementRange());
			stack->addNewBonus(std::make_shared<Bonus>(bonus));
		};
		immobilize(active);
		immobilize(target);
		immobilize(enemy);

		beginCombat();
		ASSERT_TRUE(advanceUntilNextActivation(active));

		callback = std::make_shared<ChaosBlinkCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<ChaosBlinkEnvironment>(gameState());
	}

	void expectAICastAndLegalAuthoritativeLanding(bool blinkmaster)
	{
		const auto spell = chaosBlinkSpell();
		const auto * blink = spell.toSpell();
		ASSERT_NE(blink, nullptr);
		ASSERT_NE(active, nullptr);

		spells::BattleCast targetPreview(battle(), attackerSideHero, spells::Mode::HERO, blink);
		const auto mechanics = blink->battleMechanics(&targetPreview);
		const auto viableTargets = SpellTargetEvaluator::getViableTargets(mechanics.get());
		ASSERT_FALSE(viableTargets.empty());
		for(const auto & aim : viableTargets)
		{
			ASSERT_EQ(aim.size(), 1u);
			ASSERT_NE(aim.front().unitValue, nullptr);
			const auto preview = newHorizonsBlink::preview(*mechanics, aim.front().unitValue);
			ASSERT_TRUE(preview);
			EXPECT_FALSE(preview->legalDestinations.empty());
			EXPECT_EQ(preview->blinkmaster, blinkmaster);
		}

		std::vector<StackSnapshot> before;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			before.push_back({unit->unitId(), unit->getPosition(), unit->getCount(), unit->getAvailableHealth()});
		const auto manaBefore = attackerSideHero->getManaAvailable();
		auto * liveRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
		ASSERT_NE(liveRng, nullptr);
		const auto rngBefore = randomState(*liveRng);

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
		ASSERT_NE(submittedTarget.front().unitValue, nullptr);
		const auto targetId = submittedTarget.front().unitValue->unitId();
		const auto beforeTarget = std::find_if(before.begin(), before.end(), [targetId](const StackSnapshot & snapshot)
		{
			return snapshot.id == targetId;
		});
		ASSERT_NE(beforeTarget, before.end());

		spells::BattleCast selectedPreview(battle(), attackerSideHero, spells::Mode::HERO, blink);
		const auto selectedMechanics = blink->battleMechanics(&selectedPreview);
		const auto * selectedUnit = battle()->battleGetUnitByID(targetId);
		ASSERT_NE(selectedUnit, nullptr);
		const auto selectedDestinations = newHorizonsBlink::preview(*selectedMechanics, selectedUnit);
		ASSERT_TRUE(selectedDestinations);
		EXPECT_EQ(selectedDestinations->blinkmaster, blinkmaster);
		const auto distribution = newHorizonsBlink::outcomeDistribution(*selectedDestinations, beforeTarget->position);
		ASSERT_EQ(distribution.size(), selectedDestinations->legalDestinations.size());
		uint64_t totalWeight = 0;
		for(const auto & outcome : distribution)
		{
			EXPECT_TRUE(vstd::contains(selectedDestinations->legalDestinations, outcome.hex));
			EXPECT_GT(outcome.weight, 0u);
			EXPECT_EQ(outcome.totalWeight, distribution.front().totalWeight);
			totalWeight += outcome.weight;
		}
		EXPECT_EQ(totalWeight, distribution.front().totalWeight);
		if(blinkmaster)
			EXPECT_EQ(distribution.front().totalWeight,
				selectedDestinations->legalDestinations.size() * selectedDestinations->legalDestinations.size());
		else
			EXPECT_EQ(distribution.front().totalWeight, selectedDestinations->legalDestinations.size());

		for(const auto & snapshot : before)
		{
			const auto * unchanged = battle()->battleGetUnitByID(snapshot.id);
			ASSERT_NE(unchanged, nullptr);
			EXPECT_EQ(unchanged->getPosition(), snapshot.position);
			EXPECT_EQ(unchanged->getCount(), snapshot.count);
			EXPECT_EQ(unchanged->getAvailableHealth(), snapshot.health);
		}
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
		EXPECT_EQ(randomState(*liveRng), rngBefore)
			<< "AI evaluation must use the exact expectation, not consume the authoritative landing RNG";

		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		const auto * movedTarget = battle()->battleGetUnitByID(targetId);
		ASSERT_NE(movedTarget, nullptr);
		EXPECT_NE(movedTarget->getPosition(), beforeTarget->position);
		EXPECT_TRUE(vstd::contains(selectedDestinations->legalDestinations, movedTarget->getPosition()))
			<< "Authoritative Blink must resolve inside the shared legal endpoint set, not at an AI-chosen midpoint";
		EXPECT_EQ(movedTarget->getCount(), beforeTarget->count);
		EXPECT_EQ(movedTarget->getAvailableHealth(), beforeTarget->health);
		EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	}
};

TEST_F(NewHorizonsChaosBlinkAITest, SubmitsBlinkUsingUniformLegalLandingExpectationWithoutMutatingLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	expectAICastAndLegalAuthoritativeLanding(false);
}

TEST_F(NewHorizonsChaosBlinkAITest, SubmitsBlinkmasterUsingExactTwoDrawExpectationWithoutMutatingLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	expectAICastAndLegalAuthoritativeLanding(true);
}
