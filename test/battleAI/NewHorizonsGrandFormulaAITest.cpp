/*
 * NewHorizonsGrandFormulaAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"

#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto SPELLCRAFT_SKILL = "new-horizons:spellcraft";
constexpr auto ARCANE_FOCUS_PERK = "new-horizons:spellcraft.arcaneFocus";
constexpr auto EMPOWER_SPELL_PERK = "new-horizons:spellcraft.empowerSpell";
constexpr auto GRAND_FORMULA_PERK = "new-horizons:spellcraft.grandFormula";
constexpr auto SORCERY_MAGIC_SKILL = "new-horizons:sorceryMagic";

class GrandFormulaEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit GrandFormulaEnvironment(std::shared_ptr<CGameState> state_)
		: state(std::move(state_))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class GrandFormulaAICallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	GrandFormulaAICallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{
	}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::vector<std::byte> serializedRandomState(CRandomGenerator & generator)
{
	CMemorySerializer serializer;
	serializer.oser & generator;
	return serializer.extractBuffer();
}

std::vector<std::byte> serializedGameState(CGameState & state)
{
	CMemorySerializer serializer;
	serializer.oser & state;
	return serializer.extractBuffer();
}
}

class NewHorizonsGrandFormulaAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<GrandFormulaEnvironment> environment;
	std::shared_ptr<GrandFormulaAICallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		newHorizonsMagic::validateRules(magicRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare()
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:solmyr")));

		const auto spellcraft = SecondarySkill(SecondarySkill::decode(SPELLCRAFT_SKILL));
		const auto sorcery = SecondarySkill(SecondarySkill::decode(SORCERY_MAGIC_SKILL));
		ASSERT_TRUE(spellcraft.hasValue());
		ASSERT_TRUE(sorcery.hasValue());
		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, ARCANE_FOCUS_PERK});
		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, EMPOWER_SPELL_PERK});
		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, GRAND_FORMULA_PERK});
		attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->hasActivePerk(SPELLCRAFT_SKILL, ARCANE_FOCUS_PERK));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(SPELLCRAFT_SKILL, GRAND_FORMULA_PERK));

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::IMPLOSION));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 2500, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!removed.changedStacks.empty())
			gameHandler->sendAndApply(removed);

		active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<GrandFormulaAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<GrandFormulaEnvironment>(gameState());
	}

	std::unique_ptr<spells::Mechanics> implosionMechanics(const CBattleInfoCallback * battleCallback) const
	{
		const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
		if(!spell)
			return nullptr;
		spells::BattleCast cast(battleCallback, attackerSideHero, spells::Mode::HERO, spell);
		return spell->battleMechanics(&cast);
	}
};

TEST_F(NewHorizonsGrandFormulaAITest, EvaluatorUsesLevelFourFormulaAndDetachedForecastPreservesLiveState)
{
	prepare();
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_EQ(battle()->battleGetSpellLevel(spell->getId()), 4);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(),
		spells::Mode::HERO, attackerSideHero));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));

	const auto normalCoefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId());
	ASSERT_EQ(normalCoefficient, 18'850);
	auto liveMechanics = implosionMechanics(callback->getBattle(BattleID(0)).get());
	ASSERT_NE(liveMechanics, nullptr);
	EXPECT_EQ(liveMechanics->getCastSpellPowerComponentBonusPercent(), 80);
	EXPECT_EQ(liveMechanics->getSpellPowerCoefficientBasisPoints(), normalCoefficient * 180 / 100);
	EXPECT_EQ(liveMechanics->getEmpowerSpellBonusPercent(),
		newHorizonsMagic::SPELLCRAFT_EMPOWER_BONUS_PERCENT);

	const auto liveStateBeforeForecast = serializedGameState(*gameState());
	auto * liveRng = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(liveRng, nullptr);
	const auto liveRngBeforeForecast = serializedRandomState(*liveRng);
	const auto liveMana = attackerSideHero->getManaAvailable();
	const auto enemyHealth = enemy->getAvailableHealth();
	const auto activeHealth = active->getAvailableHealth();
	const auto liveCompletedLevels = battle()->getSide(BattleSide::ATTACKER).completedHeroSpellLevels;

	const auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(environment.get(), liveCallback);
	const auto * projectedEnemy = projection.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedEnemy, nullptr);
	const spells::Target target{spells::Destination(projectedEnemy)};
	spells::BattleCast firstCast(&projection, attackerSideHero, spells::Mode::HERO, spell);
	const auto firstMechanics = spell->battleMechanics(&firstCast);
	ASSERT_NE(firstMechanics, nullptr);
	EXPECT_EQ(firstMechanics->getCastSpellPowerComponentBonusPercent(), 80);
	ASSERT_TRUE(firstMechanics->canBeCastAt(target));
	firstMechanics->castEval(projection.getServerCallback(), target);
	EXPECT_TRUE(projection.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(projection.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));

	projection.nextRound();
	spells::BattleCast laterCast(&projection, attackerSideHero, spells::Mode::HERO, spell);
	const auto laterMechanics = spell->battleMechanics(&laterCast);
	ASSERT_NE(laterMechanics, nullptr);
	EXPECT_EQ(laterMechanics->getCastSpellPowerComponentBonusPercent(), 0)
		<< "A detached accepted Level 4 forecast consumes the shared Level 4/5 gate in that model";
	EXPECT_EQ(laterMechanics->getSpellPowerCoefficientBasisPoints(), normalCoefficient);

	EXPECT_EQ(serializedGameState(*gameState()), liveStateBeforeForecast);
	EXPECT_EQ(serializedRandomState(*liveRng), liveRngBeforeForecast);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), liveMana);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
	EXPECT_EQ(active->getAvailableHealth(), activeHealth);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).completedHeroSpellLevels, liveCompletedLevels);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, SpellID(SpellID::IMPLOSION));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), liveMana);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
	EXPECT_EQ(serializedGameState(*gameState()), liveStateBeforeForecast);
	EXPECT_EQ(serializedRandomState(*liveRng), liveRngBeforeForecast)
		<< "AI evaluation and its detached spell forecasts do not consume live battle RNG";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	auto afterAcceptedCast = implosionMechanics(callback->getBattle(BattleID(0)).get());
	ASSERT_NE(afterAcceptedCast, nullptr);
	EXPECT_EQ(afterAcceptedCast->getCastSpellPowerComponentBonusPercent(), 0)
		<< "Only the accepted authoritative Level 4 cast consumes Grand Formula";
}
