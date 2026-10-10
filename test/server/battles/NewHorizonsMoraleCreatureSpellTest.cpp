/*
 * NewHorizonsMoraleCreatureSpellTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include <limits>
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/effects/Effects.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#endif

namespace
{
class NewHorizonsMoraleCreatureSpellTest : public BattleTestFixture
{
protected:
	int percent = 75;
	CStack * actor = nullptr;
	CStack * target = nullptr;
	const SpellID arrow{SpellID::MAGIC_ARROW};

	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::COMBAT_MORALE_EXTRA_DAMAGE_PERCENT, JsonNode(percent));
		// The actual saved-v3 Morale producer reads this captured curve, not only COMBAT_*.
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		for(auto & chance : rules["morale"]["goodChance"].Vector())
			chance.Integer() = 100;
		rules.setOverrideFlag(true);
		newHorizonsMagic::validateRules(rules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare()
	{
		startGame(); startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		// Existing original content, with genuine offensive SPELLCASTER abilities and charges.
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:fairieDragon"), BattleHex(4, 5), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(11, 5), 100000);
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::MORALE, BonusSource::OTHER, 10, BonusSourceID()));
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, 1000,
			BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		ASSERT_FALSE(battle()->battleIsFinished().has_value());
		ASSERT_EQ(battle()->getMagicRules()["morale"]["goodChance"].Vector().back().Integer(), 100);
		ASSERT_TRUE(actor->hasBonusOfType(BonusType::SPELLCASTER, BonusSubtypeID(arrow)));
		ASSERT_GT(actor->casts.available(), 1);
	}

	bool castActive()
	{
		battle::Target destination;
		destination.emplace_back(target);
		const auto action = BattleAction::makeCreatureSpellcast(actor, destination, arrow);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void activateMorale()
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = actor->unitId();
		active.reason = BattleUnitTurnReason::MORALE;
		gameHandler->sendAndApply(active);
	}

	std::unique_ptr<spells::Mechanics> mechanics(spells::Mode mode, const spells::Caster * caster = nullptr)
	{
		spells::BattleCast cast(battle(), caster ? caster : actor, mode, arrow.toSpell());
		cast.setSpellLevel(MasteryLevel::ADVANCED);
		return arrow.toSpell()->battleMechanics(&cast);
	}

	int64_t preview(spells::Mechanics * context, bool indirect = false)
	{
		JsonNode effect;
		effect["damage"]["type"].String() = "core:damage";
		effect["damage"]["indirect"].Bool() = indirect;
		const auto effects = spells::effects::Effects::loadJson(effect, "core", "magicArrow");
		battle::Target destination;
		destination.emplace_back(target);
		return -effects.at("damage")->getHealthChange(context, destination).hpDelta;
	}
};
#if ENABLE_BATTLE_AI
class MoraleSpellEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MoraleSpellEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}

TEST_F(NewHorizonsMoraleCreatureSpellTest, ActualEarnedMoraleCastPaysChargeAndUsesFinalSeventyFivePercent)
{
	prepare();
	const auto full = mechanics(spells::Mode::CREATURE_ACTIVE)->adjustEffectValue(target);
	ASSERT_GT(full, 0);
	const auto charges = actor->casts.available();
	const auto mana = attackerSideHero->getManaAvailable();
	auto before = target->getAvailableHealth();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full);
	EXPECT_EQ(actor->casts.available(), charges - 1);
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(battle()->battleIsMoraleExtraActivation(actor));
	EXPECT_FALSE(actor->castSpellThisTurn); // Genuine earned activation permits a second charged cast.
	auto extra = mechanics(spells::Mode::CREATURE_ACTIVE);
	EXPECT_EQ(extra->getDirectCreatureActivationDamagePercent(), 75);
	EXPECT_EQ(extra->adjustEffectValue(target), full); // Raw spell strength is unchanged.
	EXPECT_EQ(preview(extra.get()), full * 75 / 100);
	before = target->getAvailableHealth();
	const auto activationCount = server.stackActivations.size();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full * 75 / 100);
	EXPECT_EQ(actor->casts.available(), charges - 2);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_FALSE(battle()->battleIsMoraleExtraActivation(actor));
	for(size_t i = activationCount; i < server.stackActivations.size(); ++i)
		EXPECT_FALSE(server.stackActivations[i].stack == actor->unitId()
			&& server.stackActivations[i].reason == BattleUnitTurnReason::MORALE);
}

TEST_F(NewHorizonsMoraleCreatureSpellTest, ExplicitOriginalHundredPercentKeepsBothPaidCastsFullStrength)
{
	percent = 100; prepare();
	const auto full = mechanics(spells::Mode::CREATURE_ACTIVE)->adjustEffectValue(target);
	auto before = target->getAvailableHealth();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full);
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	EXPECT_TRUE(actor->hadMorale);
	EXPECT_FALSE(actor->moraleExtraActivation);
	before = target->getAvailableHealth();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full);
}

TEST_F(NewHorizonsMoraleCreatureSpellTest, HeroCastDoesNotInheritActiveCreaturesMorale)
{
	prepare(); activateMorale();
	auto hero = mechanics(spells::Mode::HERO, attackerSideHero);
	EXPECT_EQ(hero->getDirectCreatureActivationDamagePercent(), 100);
	EXPECT_EQ(preview(hero.get()), hero->adjustEffectValue(target));
}

TEST_F(NewHorizonsMoraleCreatureSpellTest, PassiveEnchanterMirrorAndSpellLikeAttackRemainUnscaled)
{
	prepare(); activateMorale();
	for(const auto mode : {spells::Mode::PASSIVE, spells::Mode::ENCHANTER,
		spells::Mode::MAGIC_MIRROR, spells::Mode::SPELL_LIKE_ATTACK})
	{
		auto context = mechanics(mode);
		EXPECT_EQ(context->getDirectCreatureActivationDamagePercent(), 100);
		EXPECT_EQ(preview(context.get()), context->adjustEffectValue(target));
	}
}

TEST_F(NewHorizonsMoraleCreatureSpellTest, ExplicitIndirectEffectKeepsFullDamageDuringActiveMoraleCast)
{
	prepare(); activateMorale();
	auto context = mechanics(spells::Mode::CREATURE_ACTIVE);
	ASSERT_EQ(context->getDirectCreatureActivationDamagePercent(), 75);
	const auto full = context->adjustEffectValue(target);
	EXPECT_EQ(preview(context.get(), true), full);
	EXPECT_EQ(preview(context.get(), false), full * 75 / 100);
}

TEST_F(NewHorizonsMoraleCreatureSpellTest, NonActiveCasterAndWarMachineDoNotInheritMorale)
{
	prepare(); activateMorale();
	battle()->activeStack = target->unitId();
	EXPECT_EQ(mechanics(spells::Mode::CREATURE_ACTIVE)->getDirectCreatureActivationDamagePercent(), 100);
	battle()->activeStack = actor->unitId();
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SIEGE_WEAPON, BonusSource::OTHER, 0, BonusSourceID()));
	EXPECT_EQ(mechanics(spells::Mode::CREATURE_ACTIVE)->getDirectCreatureActivationDamagePercent(), 100);
}

TEST_F(NewHorizonsMoraleCreatureSpellTest, HealingEffectUsesUnchangedSpellStrengthDuringMorale)
{
	prepare();
	auto injured = actor->acquireState();
	int64_t injuryDamage = 10;
	injured->damage(injuryDamage);
	UnitChanges injury(actor->unitId(), UnitChanges::EOperation::UPDATE);
	injury.data = injured->save();
	injury.healthDelta = -10;
	BattleUnitsChanged changed;
	changed.battleID = BattleID(0);
	changed.changedStacks.push_back(std::move(injury));
	gameHandler->sendAndApply(changed);
	const auto health = actor->getAvailableHealth();
	JsonNode effect;
	effect["heal"]["type"].String() = "core:heal";
	effect["heal"]["healLevel"].String() = "heal";
	effect["heal"]["healPower"].String() = "permanent";
	const auto effects = spells::effects::Effects::loadJson(effect, "core", "cure");
	battle::Target destination;
	destination.emplace_back(actor);
	auto normal = mechanics(spells::Mode::CREATURE_ACTIVE);
	const auto healing = effects.at("heal")->getHealthChange(normal.get(), destination).hpDelta;
	ASSERT_GT(healing, 0);
	activateMorale();
	auto extra = mechanics(spells::Mode::CREATURE_ACTIVE);
	ASSERT_EQ(extra->getDirectCreatureActivationDamagePercent(), 75);
	EXPECT_EQ(extra->getEffectValue(), normal->getEffectValue());
	EXPECT_EQ(effects.at("heal")->getHealthChange(extra.get(), destination).hpDelta, healing);
	EXPECT_EQ(actor->getAvailableHealth(), health);
}

TEST_F(NewHorizonsMoraleCreatureSpellTest, FinalHpFloorIsExactWithoutInt64ProductOverflow)
{
	prepare(); activateMorale();
	auto extra = mechanics(spells::Mode::CREATURE_ACTIVE);
	const auto maximum = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(extra->adjustDirectCreatureActivationDamage(maximum),
		(maximum / 100) * 75 + (maximum % 100) * 75 / 100);
	EXPECT_EQ(extra->adjustDirectCreatureActivationDamage(3), 2);
	EXPECT_EQ(extra->adjustDirectCreatureActivationDamage(1), 0);
	EXPECT_EQ(extra->adjustDirectCreatureActivationDamage(0), 0);
}

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsMoraleCreatureSpellTest, DetachedCastEvaluationMatchesPreviewAndPreservesParentSiblingAndLive)
{
	prepare(); activateMorale();
	MoraleSpellEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	HypotheticBattle child(&environment, parent);
	HypotheticBattle sibling(&environment, parent);
	const auto before = target->getAvailableHealth();
	const auto full = mechanics(spells::Mode::CREATURE_ACTIVE)->adjustEffectValue(target);
	const auto liveCharges = actor->casts.available();
	const auto * localActor = child.battleGetUnitByID(actor->unitId());
	const auto * localTarget = child.battleGetUnitByID(target->unitId());
	spells::BattleCast cast(&child, localActor, spells::Mode::CREATURE_ACTIVE, arrow.toSpell());
	cast.setSpellLevel(MasteryLevel::ADVANCED);
	auto context = arrow.toSpell()->battleMechanics(&cast);
	EXPECT_EQ(context->getDirectCreatureActivationDamagePercent(), 75);
	battle::Target destination;
	destination.emplace_back(localTarget);
	context->castEval(child.getServerCallback(), destination);
	EXPECT_EQ(before - child.battleGetUnitByID(target->unitId())->getAvailableHealth(), full * 75 / 100);
	EXPECT_EQ(parent->battleGetUnitByID(target->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(sibling.battleGetUnitByID(target->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(actor->casts.available(), liveCharges);
}
#endif
