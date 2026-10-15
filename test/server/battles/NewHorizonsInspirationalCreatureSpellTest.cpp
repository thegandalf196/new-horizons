/*
 * NewHorizonsInspirationalCreatureSpellTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/effects/Effects.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#endif

namespace
{
class NewHorizonsInspirationalCreatureSpellTest : public BattleTestFixture
{
protected:
	int output = 75;
	CStack * actor = nullptr;
	CStack * target = nullptr;
	const SpellID arrow{SpellID::MAGIC_ARROW};
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::COMBAT_MORALE_EXTRA_DAMAGE_PERCENT, JsonNode(output));
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		for(auto & chance : rules["morale"]["goodChance"].Vector())
			chance.Integer() = 100;
		rules.setOverrideFlag(true);
		newHorizonsMagic::validateRules(rules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(bool selected = true)
	{
		startGame();
		if(selected)
		{
			const SecondarySkill discipline(SecondarySkill::decode("new-horizons:discipline"));
			attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:fairieDragon"), BattleHex(4, 5), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(11, 5), 100000);
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MORALE,
			BonusSource::OTHER, 10, BonusSourceID()));
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		ASSERT_TRUE(actor->hasBonusOfType(BonusType::SPELLCASTER, BonusSubtypeID(arrow)));
		ASSERT_GT(actor->casts.available(), 1);
	}
	bool castActive()
	{
		battle::Target destination;
		destination.emplace_back(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeCreatureSpellcast(actor, destination, arrow));
	}
	std::unique_ptr<spells::Mechanics> mechanics(spells::Mode mode = spells::Mode::CREATURE_ACTIVE,
		const spells::Caster * caster = nullptr, int64_t fixed = -1)
	{
		spells::BattleCast cast(battle(), caster ? caster : actor, mode, arrow.toSpell());
		cast.setSpellLevel(MasteryLevel::ADVANCED);
		if(fixed >= 0)
			cast.setEffectValue(fixed);
		return arrow.toSpell()->battleMechanics(&cast);
	}
	int64_t preview(spells::Mechanics * context, bool indirect = false)
	{
		JsonNode json;
		json["damage"]["type"].String() = "core:damage";
		json["damage"]["indirect"].Bool() = indirect;
		const auto effects = spells::effects::Effects::loadJson(json, "core", "magicArrow");
		battle::Target destination;
		destination.emplace_back(target);
		return -effects.at("damage")->getHealthChange(context, destination).hpDelta;
	}
	void earnMorale()
	{
		ASSERT_TRUE(castActive());
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		ASSERT_TRUE(actor->hadMorale);
		ASSERT_EQ(actor->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST,
			BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)), 10);
	}
#if ENABLE_BATTLE_AI
	void expectInspirationalPair(const battle::Unit * unit, int amount)
	{
		ASSERT_NE(unit, nullptr);
		const BonusSourceID source(attackerSideHero->id);
		for(const auto subtype : {BonusCustomSubtype::damageTypeMelee, BonusCustomSubtype::damageTypeRanged})
		{
			const CSelector selector([source, subtype](const Bonus * bonus)
			{
				return bonus && bonus->source == BonusSource::HERO_SPECIAL && bonus->sid == source
					&& bonus->duration == BonusDuration::STACK_ACTIVATION
					&& bonus->stacking == "new-horizons:discipline.inspirationalLeader"
					&& bonus->type == BonusType::PERCENTAGE_DAMAGE_BOOST
					&& bonus->subtype == BonusSubtypeID(subtype);
			});
			const auto bonuses = unit->getBonuses(selector);
			EXPECT_EQ(bonuses->size(), amount == 0 ? 0u : 1u);
			if(!bonuses->empty())
			{
				EXPECT_EQ(bonuses->front()->val, amount);
			}
		}
	}
	void expectProjectedMultiplier(HypotheticBattle & model, int percent)
	{
		const auto * caster = model.battleGetUnitByID(actor->unitId());
		ASSERT_NE(caster, nullptr);
		spells::BattleCast cast(&model, caster, spells::Mode::CREATURE_ACTIVE, arrow.toSpell());
		cast.setSpellLevel(MasteryLevel::ADVANCED);
		auto context = arrow.toSpell()->battleMechanics(&cast);
		EXPECT_EQ(context->getInspirationalLeaderCreatureDamagePercent(), percent);
		EXPECT_EQ(context->getDirectCreatureActivationDamagePercent(), 75);
	}
	auto randomizerState()
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		return wire.extractBuffer();
	}
#endif
};
#if ENABLE_BATTLE_AI
class InspirationalEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit InspirationalEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}

TEST_F(NewHorizonsInspirationalCreatureSpellTest, EarnedBonusComposesOnceWithMoraleOnActualChargedCastAndExpires)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto full = mechanics()->adjustEffectValue(target);
	const auto charges = actor->casts.available();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto firstHealth = target->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(earnMorale());
	EXPECT_EQ(firstHealth - target->getAvailableHealth(), full);
	auto boosted = mechanics();
	EXPECT_EQ(boosted->adjustEffectValue(target), full);
	EXPECT_EQ(boosted->getDirectCreatureActivationDamagePercent(), 75);
	EXPECT_EQ(boosted->getInspirationalLeaderCreatureDamagePercent(), 110);
	const auto expected = full * 75 * 110 / 10000;
	EXPECT_EQ(preview(boosted.get()), expected);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), expected);
	EXPECT_EQ(actor->casts.available(), charges - 2);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(actor->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)), 0);
	EXPECT_EQ(mechanics()->getInspirationalLeaderCreatureDamagePercent(), 100);
}

TEST_F(NewHorizonsInspirationalCreatureSpellTest, HundredPercentOriginStillGainsTenPercentOnActualCreatureCast)
{
	output = 100;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto full = mechanics()->adjustEffectValue(target);
	ASSERT_NO_FATAL_FAILURE(earnMorale());
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full * 110 / 100);
}

TEST_F(NewHorizonsInspirationalCreatureSpellTest, FixedDamageIsBoostedButCombinedOutputHasOnlyOneFloor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(earnMorale());
	auto fixed = mechanics(spells::Mode::CREATURE_ACTIVE, nullptr, 101);
	ASSERT_EQ(fixed->adjustEffectValue(target), 101);
	EXPECT_EQ(preview(fixed.get()), int64_t(101) * 75 * 110 / 10000);
	EXPECT_NE(preview(fixed.get()), (int64_t(101) * 75 / 100) * 110 / 100);
	EXPECT_EQ(preview(fixed.get(), true), 101);
}

TEST_F(NewHorizonsInspirationalCreatureSpellTest, UnselectedActualMoraleCastDoesNotAcquireTheMultiplier)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto full = mechanics()->adjustEffectValue(target);
	ASSERT_TRUE(castActive());
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	EXPECT_EQ(mechanics()->getInspirationalLeaderCreatureDamagePercent(), 100);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full * 75 / 100);
}

TEST_F(NewHorizonsInspirationalCreatureSpellTest, HistoricalAndUnrelatedPercentageBoostsRemainAttackOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto oldBonus = std::make_shared<Bonus>(BonusDuration::STACK_ACTIVATION,
		BonusType::PERCENTAGE_DAMAGE_BOOST, BonusSource::HERO_SPECIAL, 10,
		BonusSourceID(attackerSideHero->id), BonusSubtypeID(BonusCustomSubtype::damageTypeMelee));
	oldBonus->description.appendRawString("New Horizons: Inspirational Leader");
	actor->addNewBonus(oldBonus);
	EXPECT_EQ(mechanics()->getInspirationalLeaderCreatureDamagePercent(), 100);
	auto unrelated = std::make_shared<Bonus>(*oldBonus);
	unrelated->stacking = "unrelated:damage";
	actor->addNewBonus(unrelated);
	EXPECT_EQ(mechanics()->getInspirationalLeaderCreatureDamagePercent(), 100);
}

TEST_F(NewHorizonsInspirationalCreatureSpellTest, HeroPassiveReflectedSpellLikeAndNonActiveCastersRemainUnboosted)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(earnMorale());
	for(const auto mode : {spells::Mode::PASSIVE, spells::Mode::ENCHANTER,
		spells::Mode::MAGIC_MIRROR, spells::Mode::SPELL_LIKE_ATTACK})
	{
		auto context = mechanics(mode);
		EXPECT_EQ(context->getInspirationalLeaderCreatureDamagePercent(), 100);
		EXPECT_EQ(preview(context.get()), context->adjustEffectValue(target));
	}
	auto hero = mechanics(spells::Mode::HERO, attackerSideHero);
	EXPECT_EQ(hero->getInspirationalLeaderCreatureDamagePercent(), 100);
	EXPECT_EQ(preview(hero.get()), hero->adjustEffectValue(target));
	const auto bonuses = actor->getBonuses(Selector::typeSubtype(BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	ASSERT_FALSE(bonuses->empty());
	for(const auto & bonus : *bonuses)
		if(bonus->stacking == "new-horizons:discipline.inspirationalLeader")
			target->addNewBonus(std::make_shared<Bonus>(*bonus));
	EXPECT_EQ(mechanics(spells::Mode::CREATURE_ACTIVE, target)->getInspirationalLeaderCreatureDamagePercent(), 100);
}

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsInspirationalCreatureSpellTest, DetachedCastMatchesActualDamageAndLeavesParentSiblingLiveUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(earnMorale());
	const auto full = mechanics()->adjustEffectValue(target);
	const auto expected = full * 75 * 110 / 10000;
	const auto before = target->getAvailableHealth();
	const auto charges = actor->casts.available();
	InspirationalEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	HypotheticBattle child(&environment, parent);
	HypotheticBattle sibling(&environment, parent);
	spells::BattleCast cast(&child, child.battleGetUnitByID(actor->unitId()), spells::Mode::CREATURE_ACTIVE, arrow.toSpell());
	cast.setSpellLevel(MasteryLevel::ADVANCED);
	auto context = arrow.toSpell()->battleMechanics(&cast);
	EXPECT_EQ(context->getInspirationalLeaderCreatureDamagePercent(), 110);
	battle::Target destination;
	destination.emplace_back(child.battleGetUnitByID(target->unitId()));
	ASSERT_TRUE(context->canBeCastAt(destination));
	context->castEval(child.getServerCallback(), destination);
	EXPECT_EQ(before - child.battleGetUnitByID(target->unitId())->getAvailableHealth(), expected);
	EXPECT_EQ(parent->battleGetUnitByID(target->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(sibling.battleGetUnitByID(target->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(actor->casts.available(), charges);
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), expected);
}


TEST_F(NewHorizonsInspirationalCreatureSpellTest,
	ChildrenKeepInspirationalProducerUntilOwnActivationEndAfterParentExpires)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(earnMorale());
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(actor, 10));
	// Warm the ordinary lazy retaliation latch before recording exact state.
	(void)actor->counterAttacks.total();
	(void)target->counterAttacks.total();
	const auto liveActor = actor->acquireState()->save();
	const auto liveTarget = target->acquireState()->save();
	const auto rngBefore = randomizerState();
	InspirationalEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(child->battleGetUnitByID(actor->unitId()), 10));
	// Sibling has no explicit unit/bonus query before this completion boundary.
	// Use the same selector as forecast activation-end adapters, not nextRound().
	parent->getForUpdate(actor->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(parent->battleGetUnitByID(actor->unitId()), 0));
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(child->battleGetUnitByID(actor->unitId()), 10));
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(sibling->battleGetUnitByID(actor->unitId()), 10));
	ASSERT_NO_FATAL_FAILURE(expectProjectedMultiplier(*child, 110));
	ASSERT_NO_FATAL_FAILURE(expectProjectedMultiplier(*sibling, 110));
	ASSERT_NO_FATAL_FAILURE(expectProjectedMultiplier(*parent, 100));
	child->getForUpdate(actor->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
	for(int query = 0; query < 2; ++query)
	{
		ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(child->battleGetUnitByID(actor->unitId()), 0));
		ASSERT_NO_FATAL_FAILURE(expectProjectedMultiplier(*child, 100));
	}
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(parent->battleGetUnitByID(actor->unitId()), 0));
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(sibling->battleGetUnitByID(actor->unitId()), 10));
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(actor, 10));
	for(const auto & model : {parent, child, sibling})
	{
		EXPECT_EQ(model->getForUpdate(actor->unitId())->save(), liveActor);
		EXPECT_EQ(model->getForUpdate(target->unitId())->save(), liveTarget);
	}
	EXPECT_EQ(actor->acquireState()->save(), liveActor);
	EXPECT_EQ(target->acquireState()->save(), liveTarget);
	EXPECT_EQ(randomizerState(), rngBefore);
}

TEST_F(NewHorizonsInspirationalCreatureSpellTest,
	DetachedInspirationalProducerSurvivesLiveActivationEndUntilOwnExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(earnMorale());
	(void)actor->counterAttacks.total();
	(void)target->counterAttacks.total();
	const auto projectedActor = actor->acquireState()->save();
	const auto projectedTarget = target->acquireState()->save();
	const auto full = mechanics()->adjustEffectValue(target);
	InspirationalEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	// Freeze target scalar state before the actual accepted spell changes HP.
	for(const auto & model : {parent, child, sibling})
	{
		EXPECT_EQ(model->getForUpdate(target->unitId())->save(), projectedTarget);
	}
	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), full * 75 * 110 / 10000);
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(actor, 0));
	const auto liveActorAfter = actor->acquireState()->save();
	const auto liveTargetAfter = target->acquireState()->save();
	// The authoritative action may legitimately draw RNG. Subsequent detached
	// observations/expiry must not draw any additional authoritative randomness.
	const auto rngAfterLive = randomizerState();
	for(const auto & model : {parent, child, sibling})
	{
		ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(model->battleGetUnitByID(actor->unitId()), 10));
		ASSERT_NO_FATAL_FAILURE(expectProjectedMultiplier(*model, 110));
	}
	child->getForUpdate(actor->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(child->battleGetUnitByID(actor->unitId()), 0));
	ASSERT_NO_FATAL_FAILURE(expectProjectedMultiplier(*child, 100));
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(parent->battleGetUnitByID(actor->unitId()), 10));
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(sibling->battleGetUnitByID(actor->unitId()), 10));
	for(const auto & model : {parent, child, sibling})
	{
		EXPECT_EQ(model->getForUpdate(actor->unitId())->save(), projectedActor);
		EXPECT_EQ(model->getForUpdate(target->unitId())->save(), projectedTarget);
	}
	EXPECT_EQ(actor->acquireState()->save(), liveActorAfter);
	EXPECT_EQ(target->acquireState()->save(), liveTargetAfter);
	ASSERT_NO_FATAL_FAILURE(expectInspirationalPair(actor, 0));
	EXPECT_EQ(randomizerState(), rngAfterLive);
}
#endif
