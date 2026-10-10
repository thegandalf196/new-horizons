/*
 * NewHorizonsImplosionAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include <algorithm>
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/spells/NewHorizonsImplosion.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"

namespace
{
class ImplosionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ImplosionEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class ImplosionCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	ImplosionCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};
class NewHorizonsImplosionAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * primary = nullptr;
	CStack * other = nullptr;
	std::shared_ptr<ImplosionEnvironment> environment;
	std::shared_ptr<ImplosionCallback> callback;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_TRUE(newHorizonsImplosion::hasRules(rules));
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void injure(CStack * unit, int64_t amount)
	{
		const auto before = unit->getAvailableHealth();
		auto state = unit->acquireState();
		state->damage(amount);
		UnitChanges change(unit->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -amount;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(changed);
		ASSERT_EQ(unit->getAvailableHealth(), before - amount);
	}

	void prepare()
	{
		useCommands = false; // Isolate the paid spell consumer, not an Order choice.
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:griffin"), BattleHex(3, 5), 200);
		primary = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(10, 5), 10000);
		other = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10000);
		ASSERT_NO_FATAL_FAILURE(injure(other, 9500));
		ASSERT_EQ(other->getAvailableHealth(), 500);
		Bonus initiative(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
		active->addNewBonus(std::make_shared<Bonus>(initiative));
		active->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
			BonusSource::OTHER, -static_cast<int32_t>(active->getMovementRange()), BonusSourceID()));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), active);
		const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
		spells::BattleCast captured(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&captured);
		ASSERT_EQ(attackerSideHero->getEffectPower(spell), 1); // Real cache value, not requested base0.
		ASSERT_EQ(mechanics->getEffectPower(), 1);
		ASSERT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 13000);
		ASSERT_EQ(mechanics->getWarcastingBonusPercent(), 0);
		ASSERT_EQ(mechanics->getEmpowerSpellBonusPercent(), 0);
		ASSERT_EQ(canonicalDamage(10000), 1213);
		ASSERT_EQ(canonicalDamage(500), 60);
		callback = std::make_shared<ImplosionCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<ImplosionEnvironment>(gameState());
	}

	int64_t canonicalDamage(int64_t health) const
	{
		// This fixture intentionally disables expanded primary ratings with Orders.
		// The legacy primary cache clamps local base SP0 to actual SP1; Advanced
		// Sorcery scales only the 0.1%-per-SP term by 130%, not the fixed12%.
		// Independent bounded integer oracle: canonical30% cap before the sole final floor.
		const int64_t power = attackerSideHero->getEffectPower(SpellID(SpellID::IMPLOSION).toSpell());
		const int64_t percentageNumerator = std::min<int64_t>(300000, 120000 + power * 10 * 130);
		return health * percentageNumerator / 1000000;
	}

	void project(HypotheticBattle & state)
	{
		const auto * target = state.battleGetUnitByID(primary->unitId());
		spells::BattleCast cast(&state, attackerSideHero, spells::Mode::HERO,
			SpellID(SpellID::IMPLOSION).toSpell());
		auto mechanics = SpellID(SpellID::IMPLOSION).toSpell()->battleMechanics(&cast);
		spells::detail::ProblemImpl problem;
		const spells::Target targets{spells::Destination(target)};
		ASSERT_TRUE(mechanics->canBeCastAt(targets, problem));
		mechanics->castEval(state.getServerCallback(), targets);
	}
};
}

TEST_F(NewHorizonsImplosionAITest, OrdinaryCandidateForecastUsesEachTargetsCurrentHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 2u);
	EXPECT_EQ(mechanics->adjustEffectValue(primary), canonicalDamage(primary->getAvailableHealth()));
	EXPECT_EQ(mechanics->adjustEffectValue(other), 60);
	EXPECT_GT(mechanics->adjustEffectValue(primary), mechanics->adjustEffectValue(other));
}

TEST_F(NewHorizonsImplosionAITest, ActualDetachedCastSharesPullAndKeepsParentSiblingAndLiveIsolated)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto health = primary->getAvailableHealth();
	const auto position = other->getPosition();
	ASSERT_NO_FATAL_FAILURE(project(*child));
	EXPECT_EQ(child->battleGetUnitByID(primary->unitId())->getAvailableHealth(), health - canonicalDamage(health));
	EXPECT_EQ(child->battleGetUnitByID(other->unitId())->getPosition(), BattleHex(11, 5));
	EXPECT_EQ(parent->battleGetUnitByID(primary->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(sibling->battleGetUnitByID(primary->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(parent->battleGetUnitByID(other->unitId())->getPosition(), position);
	EXPECT_EQ(sibling->battleGetUnitByID(other->unitId())->getPosition(), position);
	EXPECT_EQ(primary->getAvailableHealth(), health);
	EXPECT_EQ(other->getPosition(), position);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::IMPLOSION;
	action.aimToUnit(primary);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(primary->getAvailableHealth(), child->battleGetUnitByID(primary->unitId())->getAvailableHealth());
	EXPECT_EQ(other->getPosition(), child->battleGetUnitByID(other->unitId())->getPosition());
}

TEST_F(NewHorizonsImplosionAITest, PaidAIChoosesHealthyTargetAndAuthoritativeCastMatchesForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	const auto health = primary->getAvailableHealth();
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.spell, SpellID::IMPLOSION);
	const auto targets = action.getTarget(battle());
	ASSERT_EQ(targets.size(), 1u);
	ASSERT_NE(targets.front().unitValue, nullptr);
	EXPECT_EQ(targets.front().unitValue->unitId(), primary->unitId());
	EXPECT_EQ(primary->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(primary->getAvailableHealth(), health - canonicalDamage(health));
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(other->getPosition(), BattleHex(11, 5));
}
