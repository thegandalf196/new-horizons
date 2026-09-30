/*
 * NewHorizonsMisfortuneAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/CSpell.h"

namespace
{
class MisfortuneEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MisfortuneEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class MisfortuneCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	explicit MisfortuneCallback(PlayerColor player = PlayerColor(0)) : CBattleCallback(player, nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

template<typename T>
std::vector<std::byte> serializedState(T & object)
{
	CMemorySerializer serializer;
	serializer.oser & object;
	return serializer.extractBuffer();
}

Bonus luckBonus(int value)
{
	return Bonus(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::OTHER,
		value, BonusSourceID());
}

class NewHorizonsMisfortuneAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);

		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		newHorizonsMagic::validateRules(magicRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		// Remove unrelated Order incentives so the focused legal-cast fixture
		// measures Misfortune's target value rather than command coefficients.
		const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
		JsonNode commandRules = combatRules["combat"]["heroCommands"];
		for(auto & commandEntry : commandRules["commands"].Struct())
			for(auto & effectEntry : commandEntry.second["effects"].Struct())
			{
				auto & formula = effectEntry.second;
				formula["base"].Float() = 0;
				formula["attack"].Float() = 0;
				formula["defense"].Float() = 0;
			}
		heroCommands::validateRules(commandRules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void prepareMisfortuneCaster()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(SpellID::MISFORTUNE);
		const auto chaosMagicId = SecondarySkill::decode("new-horizons:chaosMagic");
		ASSERT_GE(chaosMagicId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(chaosMagicId), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
	}
};
}

TEST_F(NewHorizonsMisfortuneAITest,
	DetachedProjectionScalesFavorableChanceAndPreservesNegativeLuck)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto * friendly = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * positiveLuck = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(12, 4), 1);
	auto * negativeLuck = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(12, 6), 1);
	positiveLuck->addNewBonus(std::make_shared<Bonus>(luckBonus(3)));
	negativeLuck->addNewBonus(std::make_shared<Bonus>(luckBonus(-2)));

	auto environment = std::make_shared<MisfortuneEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(environment.get(), callback);
	const auto addMisfortune = [&](uint32_t unitId)
	{
		Bonus luckCap(BonusDuration::N_TURNS, BonusType::MAXIMUM_LUCK,
			BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::MISFORTUNE)));
		luckCap.turnsRemain = 2;
		Bonus chanceMultiplier(BonusDuration::N_TURNS,
			BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS,
			BonusSource::SPELL_EFFECT, 5000, BonusSourceID(SpellID(SpellID::MISFORTUNE)),
			BonusSubtypeID(), BonusValueType::INDEPENDENT_MIN);
		chanceMultiplier.turnsRemain = 2;
		projected.addUnitBonus(unitId, {luckCap, chanceMultiplier});
	};
	addMisfortune(positiveLuck->unitId());
	addMisfortune(negativeLuck->unitId());

	const auto projectedPositive = projected.getForUpdate(positiveLuck->unitId());
	const auto projectedNegative = projected.getForUpdate(negativeLuck->unitId());
	EXPECT_EQ(positiveLuck->favorableCreatureAbilityChanceBasisPoints(30), 3000);
	EXPECT_EQ(projectedPositive->favorableCreatureAbilityChanceBasisPoints(30), 1500);
	EXPECT_EQ(battle()->battleGetAttackLuck(positiveLuck, friendly, false), 3);
	EXPECT_EQ(projected.battleGetAttackLuck(projectedPositive.get(), friendly, false), 0);
	EXPECT_EQ(battle()->battleGetAttackLuck(negativeLuck, friendly, false), -2);
	EXPECT_EQ(projected.battleGetAttackLuck(projectedNegative.get(), friendly, false), -2);
	EXPECT_EQ(positiveLuck->valOfBonuses(BonusType::LUCK), 3);
	EXPECT_EQ(negativeLuck->valOfBonuses(BonusType::LUCK), -2);
}

TEST_F(NewHorizonsMisfortuneAITest,
	RealEvaluatorChoosesLegalMisfortuneWithoutRollingOrMutatingBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareMisfortuneCaster());
	auto * active = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * friendlyThreat = addStack(BattleSide::ATTACKER,
		creatureByName("core:angel"), BattleHex(4, 5), 12);
	auto * target = addStack(BattleSide::DEFENDER,
		creatureByName("core:angel"), BattleHex(12, 5), 30);
	target->addNewBonus(std::make_shared<Bonus>(luckBonus(3)));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::DOUBLE_DAMAGE_CHANCE, BonusSource::OTHER, 50, BonusSourceID()));

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	const SpellID misfortune(SpellID::MISFORTUNE);
	const auto * spell = misfortune.toSpell();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto friendlyHealthBefore = friendlyThreat->getAvailableHealth();
	const auto targetHealthBefore = target->getAvailableHealth();
	const auto serverRandomBefore = serializedState(*gameHandler->randomizer);
	const auto defaultRandomBefore = serializedState(CRandomGenerator::getDefault());

	auto callback = std::make_shared<MisfortuneCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MisfortuneEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0),
		BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, misfortune);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().unitValue, target->unitId());

	EXPECT_EQ(serializedState(*gameHandler->randomizer), serverRandomBefore);
	EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), defaultRandomBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(friendlyThreat->getAvailableHealth(), friendlyHealthBefore);
	EXPECT_EQ(target->getAvailableHealth(), targetHealthBefore);

	const auto cost = battle()->battleGetSpellCost(spell, attackerSideHero);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - cost);
	EXPECT_EQ(battle()->battleGetAttackLuck(target, friendlyThreat, false), 0);
	EXPECT_EQ(target->favorableCreatureAbilityChanceBasisPoints(50), 3750);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(friendlyThreat->getAvailableHealth(), friendlyHealthBefore);
	EXPECT_EQ(target->getAvailableHealth(), targetHealthBefore);
}
