/*
 * NewHorizonsHandOfFateAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/Problem.h"
#include "../../server/CGameHandler.h"

namespace
{
constexpr auto handOfFateKey = "new-horizons:handOfFate";

SpellID handOfFateSpell()
{
	return SpellID(SpellID::decode(std::string(handOfFateKey)));
}

class HandOfFateEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit HandOfFateEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class HandOfFateCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	HandOfFateCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
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
	int32_t count = 0;
	int64_t health = 0;
	BattleHex position;
};
}

class NewHorizonsHandOfFateAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * friendly = nullptr;
	CStack * primaryEnemy = nullptr;
	CStack * secondaryEnemy = nullptr;
	std::shared_ptr<HandOfFateEnvironment> environment;
	std::shared_ptr<HandOfFateCallback> callback;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
		JsonNode commandRules = combatRules["combat"]["heroCommands"];
		// This test checks Hand of Fate's legal submission and read-only forecast,
		// not whether it should beat the separate production Order heuristics.
		for(auto & command : commandRules["commands"].Struct())
			for(auto & effect : command.second["effects"].Struct())
			{
				effect.second["base"].Float() = 0;
				effect.second["attack"].Float() = 0;
				effect.second["defense"].Float() = 0;
			}
		heroCommands::validateRules(commandRules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
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
		}
		return false;
	}

	void immobilize(CStack * stack)
	{
		Bonus bonus;
		bonus.type = BonusType::STACKS_SPEED;
		bonus.duration = BonusDuration::ONE_BATTLE;
		bonus.val = -static_cast<int32_t>(stack->getMovementRange());
		stack->addNewBonus(std::make_shared<Bonus>(bonus));
	}

	void prepare()
	{
		useCommands = true;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);

		const auto spell = handOfFateSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		// Keep Orders enabled below, but give Hand of Fate an unmistakably
		// favorable target comparison: its damage should matter against the large
		// enemy armies even after accounting for its random friendly spill.
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		active = addStack(BattleSide::ATTACKER,
			creatureByName("core:stoneGolem"), BattleHex(3, 4), 1);
		friendly = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(4, 7), 1);
		primaryEnemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:ogre"), BattleHex(14, 4), 1000);
		secondaryEnemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:peasant"), BattleHex(14, 7), 1000);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(primaryEnemy, nullptr);
		ASSERT_NE(secondaryEnemy, nullptr);
		for(auto * unit : {active, friendly, primaryEnemy, secondaryEnemy})
			immobilize(unit);

		beginCombat();
		ASSERT_TRUE(advanceUntilNextActivation(active));

		callback = std::make_shared<HandOfFateCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<HandOfFateEnvironment>(gameState());
	}

	std::vector<StackSnapshot> snapshots() const
	{
		std::vector<StackSnapshot> result;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			result.push_back({unit->unitId(), unit->getCount(), unit->getAvailableHealth(), unit->getPosition()});
		return result;
	}

	void expectUnchanged(const std::vector<StackSnapshot> & before) const
	{
		for(const auto & snapshot : before)
		{
			const auto * unit = battle()->battleGetUnitByID(snapshot.id);
			ASSERT_NE(unit, nullptr);
			EXPECT_EQ(unit->getCount(), snapshot.count);
			EXPECT_EQ(unit->getAvailableHealth(), snapshot.health);
			EXPECT_EQ(unit->getPosition(), snapshot.position);
		}
	}
};

TEST_F(NewHorizonsHandOfFateAITest, ValuesUniformCollateralAndSubmitsOnlyAReadOnlyLegalCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = handOfFateSpell();
	const auto * handOfFate = spell.toSpell();
	ASSERT_NE(handOfFate, nullptr);
	ASSERT_EQ(handOfFate->getLevel(), 3);

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto before = snapshots();
	spells::BattleCast targetPreview(battle(), attackerSideHero, spells::Mode::HERO, handOfFate);
	const auto targetMechanics = handOfFate->battleMechanics(&targetPreview);
	spells::detail::ProblemImpl admission;
	const bool canCast = targetMechanics->canBeCast(admission);
	std::vector<std::string> admissionProblems;
	admission.getAll(admissionProblems);
	ASSERT_TRUE(canCast) << ::testing::PrintToString(admissionProblems);
	ASSERT_EQ(targetMechanics->getTargetTypes(), (std::vector<spells::AimType>{spells::AimType::CREATURE}));
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(targetMechanics.get());
	ASSERT_EQ(viableTargets.size(), 2u);
	for(const auto & candidate : viableTargets)
	{
		ASSERT_EQ(candidate.size(), 1u);
		ASSERT_NE(candidate.front().unitValue, nullptr);
		EXPECT_EQ(candidate.front().unitValue->unitSide(), BattleSide::DEFENDER);
	}

	const spells::Target primaryTarget{spells::Destination(primaryEnemy)};
	const auto expectedBeforeResistantRecipient = SpellTargetEvaluator::handOfFateExpectedDamageValue(
		targetMechanics.get(), primaryTarget, PlayerColor(0), callback->getBattle(BattleID(0)));
	ASSERT_TRUE(expectedBeforeResistantRecipient);
	EXPECT_GT(expectedBeforeResistantRecipient->hostileDamageValue, 0.0f);
	EXPECT_GT(expectedBeforeResistantRecipient->friendlyDamageValue, 0.0f);
	EXPECT_GT(expectedBeforeResistantRecipient->hostileDamageValue,
		expectedBeforeResistantRecipient->friendlyDamageValue)
		<< "The direct hostile hit and enemy spill should outweigh this scenario's expected friendly spill";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	expectUnchanged(before);

	// A fully resistant unit stays in the uniform spill pool and absorbs its
	// chance without causing a reroll.  Its presence therefore dilutes the
	// friendly recipient's expected share by exactly one fourth.
	auto * resistantRecipient = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(15, 9), 20);
	ASSERT_NE(resistantRecipient, nullptr);
	resistantRecipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	ASSERT_EQ(resistantRecipient->magicResistance(), 100);
	spells::BattleCast updatedPreview(battle(), attackerSideHero, spells::Mode::HERO, handOfFate);
	const auto updatedMechanics = handOfFate->battleMechanics(&updatedPreview);
	const auto expectedWithResistantRecipient = SpellTargetEvaluator::handOfFateExpectedDamageValue(
		updatedMechanics.get(), primaryTarget, PlayerColor(0), callback->getBattle(BattleID(0)));
	ASSERT_TRUE(expectedWithResistantRecipient);
	EXPECT_NEAR(expectedWithResistantRecipient->friendlyDamageValue,
		expectedBeforeResistantRecipient->friendlyDamageValue * 0.75f, 0.02f);

	std::vector<StackSnapshot> beforeAI = snapshots();
	const auto manaBeforeAI = attackerSideHero->getManaAvailable();
	const auto playerBattle = callback->getBattle(BattleID(0));
	ASSERT_EQ(playerBattle->battleGetMyHero(), attackerSideHero);
	ASSERT_TRUE(handOfFate->canBeCast(playerBattle.get(), spells::Mode::HERO, attackerSideHero));
	spells::BattleCast playerPreview(playerBattle.get(), attackerSideHero, spells::Mode::HERO, handOfFate);
	const auto playerMechanics = handOfFate->battleMechanics(&playerPreview);
	ASSERT_FALSE(SpellTargetEvaluator::getViableTargets(playerMechanics.get()).empty());
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
	EXPECT_EQ(submittedTarget.front().unitValue->unitSide(), BattleSide::DEFENDER);
	EXPECT_TRUE(std::any_of(viableTargets.begin(), viableTargets.end(), [&](const spells::Target & candidate)
	{
		return candidate.front().unitValue == submittedTarget.front().unitValue;
	})) << "BattleEvaluator must submit an ordinary legal hostile primary target";

	expectUnchanged(beforeAI);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeAI);
	EXPECT_EQ(randomState(*liveRng), rngBefore)
		<< "Expected collateral valuation must not consume live battle RNG";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBeforeAI);
}
