/*
 * NewHorizonsPreparedCasterAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto WISDOM_SKILL = "new-horizons:wisdom";
constexpr auto PREPARED_CASTER_PERK = "new-horizons:wisdom.preparedCaster";

class PreparedCasterEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PreparedCasterEnvironment(std::shared_ptr<CGameState> state_)
		: state(std::move(state_))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class PreparedCasterAICallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	PreparedCasterAICallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{
	}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsPreparedCasterAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<PreparedCasterEnvironment> environment;
	std::shared_ptr<PreparedCasterAICallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int32_t spellPower = 0)
	{
		startGame();
		const auto wisdom = SecondarySkill(SecondarySkill::decode(WISDOM_SKILL));
		ASSERT_TRUE(wisdom.hasValue());
		attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({WISDOM_SKILL, PREPARED_CASTER_PERK});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL, PREPARED_CASTER_PERK));

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!removed.changedStacks.empty())
			gameHandler->sendAndApply(removed);

		active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<PreparedCasterAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<PreparedCasterEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsPreparedCasterAITest, ProjectionCopiesAndConsumesOnlyItsOwnHeroCastState)
{
	prepare();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_EQ(attackerSideHero->getListedSpellCost(spell), 4);
	ASSERT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	const int wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(spell), 1, MasteryLevel::BASIC);
	const int preparedCost = std::max(1, wisdomCost - 2);
	EXPECT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), preparedCost);

	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	EXPECT_FALSE(liveCallback->getBattle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(liveCallback->battleGetSpellCost(spell, attackerSideHero), preparedCost);

	auto model = std::make_shared<HypotheticBattle>(environment.get(), liveCallback);
	auto beforeCastCopy = std::make_shared<HypotheticBattle>(environment.get(), model);
	EXPECT_FALSE(model->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(beforeCastCopy->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(model->battleGetSpellCost(spell, attackerSideHero), preparedCost);

	const auto targetHealth = enemy->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto castCount = battle()->battleCastSpells(BattleSide::ATTACKER);
	EXPECT_NE(model->getServerCallback()->getRNG(), getRNG());
	const auto * projectedTarget = model->battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	spells::Target target{spells::Destination(projectedTarget)};
	spells::BattleCast cast(model.get(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(model->getServerCallback(), target);

	EXPECT_TRUE(model->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(beforeCastCopy->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(liveCallback->getBattle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(model->battleGetSpellCost(spell, attackerSideHero), wisdomCost);
	EXPECT_EQ(liveCallback->battleGetSpellCost(spell, attackerSideHero), preparedCost);
	EXPECT_EQ(enemy->getAvailableHealth(), targetHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), castCount);

	auto nestedCopy = std::make_shared<HypotheticBattle>(environment.get(), model);
	EXPECT_TRUE(nestedCopy->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(nestedCopy->battleGetSpellCost(spell, attackerSideHero), wisdomCost);
	nestedCopy->nextRound();
	EXPECT_TRUE(nestedCopy->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(nestedCopy->battleGetSpellCost(spell, attackerSideHero), wisdomCost);
}

TEST_F(NewHorizonsPreparedCasterAITest, InvalidTargetHeroCastEvalDoesNotConsumePreparedCaster)
{
	prepare();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), liveCallback);
	const auto firstCost = model.battleGetSpellCost(spell, attackerSideHero);

	const spells::Target invalidTarget{spells::Destination(BattleHex::INVALID)};
	spells::BattleCast cast(&model, attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_FALSE(mechanics->canBeCastAt(invalidTarget));
	mechanics->castEval(model.getServerCallback(), invalidTarget);

	EXPECT_FALSE(model.hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(model.battleGetSpellCost(spell, attackerSideHero), firstCost);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsPreparedCasterAITest, CreatureModeCastEvalDoesNotConsumeHeroDiscount)
{
	prepare();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto * creatureSpell = SpellID(SpellID::decode("core:fireballAbility")).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_NE(creatureSpell, nullptr);
	ASSERT_TRUE(creatureSpell->isCreatureAbility());

	auto * creatureCaster = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 2), 10);
	ASSERT_NE(creatureCaster, nullptr);
	Bonus creaturePower;
	creaturePower.type = BonusType::CREATURE_SPELL_POWER;
	creaturePower.val = 100;
	creatureCaster->addNewBonus(std::make_shared<Bonus>(creaturePower));

	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), liveCallback);
	const auto firstCost = model.battleGetSpellCost(spell, attackerSideHero);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto targetHealth = enemy->getAvailableHealth();
	const auto * projectedTarget = model.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	const spells::Target target{spells::Destination(projectedTarget)};
	spells::BattleCast cast(&model, creatureCaster, spells::Mode::CREATURE_ACTIVE, creatureSpell);
	const auto mechanics = creatureSpell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(model.getServerCallback(), target);

	EXPECT_FALSE(model.hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(model.battleGetSpellCost(spell, attackerSideHero), firstCost);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(enemy->getAvailableHealth(), targetHealth);
}

TEST_F(NewHorizonsPreparedCasterAITest, EvaluatorSubmitsAHeroSpellWithoutConsumingLiveStateDuringEvaluation)
{
	prepare(9900);
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(),
		spells::Mode::HERO, attackerSideHero));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto targetHealth = enemy->getAvailableHealth();
	const auto firstCastCost = battle()->battleGetSpellCost(spell, attackerSideHero);
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(spell), 1, MasteryLevel::BASIC);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, SpellID(SpellID::MAGIC_ARROW));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(enemy->getAvailableHealth(), targetHealth);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - firstCastCost);
	EXPECT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), wisdomCost);
}
