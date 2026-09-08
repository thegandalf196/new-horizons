/*
 * NewHorizonsDirectDamageMechanicsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/Destination.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/ProxyCaster.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../server/ServerSpellCastEnvironment.h"
#include <vcmi/Environment.h>
#include <iostream>

namespace
{
constexpr auto arrowKey = "core:magicArrow";
JsonNode savedFormula(int base = 20, int coefficient = 20)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = 2;
	rules["spells"][arrowKey]["directDamage"]["base"].Integer() = base;
	rules["spells"][arrowKey]["directDamage"]["powerCoefficient"].Integer() = coefficient;
	return rules;
}

class ControlledCaster final : public spells::ProxyCaster
{
public:
	int divisor = 10;
	int64_t overrideValue = 0;
	mutable int valueReads = 0;
	mutable int divisorReads = 0;
	explicit ControlledCaster(const spells::Caster * caster) : ProxyCaster(caster) {}
	int64_t getEffectValue(const spells::Spell *) const override { ++valueReads; return overrideValue; }
	int32_t getEffectPowerDivisor(const spells::Spell *) const override { ++divisorReads; return divisor; }
};

class ConflictingMagicWorld final : public CGameInfoCallback
{
	CGameState & state;
	JsonNode rules;
public:
	ConflictingMagicWorld(CGameState & state, JsonNode rules) : state(state), rules(std::move(rules)) {}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	const JsonNode & getMagicRules() const override { return rules; }
};

class DamageEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
	const GameCb * world;
	const BattleCb * selectedBattle;
public:
	DamageEnvironment(std::shared_ptr<CGameState> state, const GameCb * world, const BattleCb * selectedBattle = nullptr)
		: state(std::move(state)), world(world), selectedBattle(selectedBattle) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override
	{
		return selectedBattle && id == BattleID(0) ? selectedBattle : state->getBattle(id);
	}
	const GameCb * game() const override { return world ? world : state.get(); }
};

// Declare before the incoming BattleInfo: restore only after its destructor has
// cleared army.battle and detached its bonus graph. Never re-localInit the original.
class RestoreArmyBattleLinks
{
	CGHeroInstance & attacker;
	CGHeroInstance & defender;
	BattleInfo * attackerBattle;
	BattleInfo * defenderBattle;
public:
	RestoreArmyBattleLinks(CGHeroInstance & attacker, CGHeroInstance & defender)
		: attacker(attacker), defender(defender), attackerBattle(attacker.battle), defenderBattle(defender.battle) {}
	~RestoreArmyBattleLinks()
	{
		attacker.battle = attackerBattle;
		defender.battle = defenderBattle;
	}
};
}

class NewHorizonsDirectDamageMechanicsTest : public HeroCommandFixture
{
protected:
	bool savedEnabled = true;
	bool forceRealHeroScale = false;
	JsonNode authoredRules = savedFormula();
	CStack * target = nullptr;
	const CSpell * spell = nullptr;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(forceRealHeroScale)
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, savedEnabled ? authoredRules : JsonNode());
	}

	void prepare()
	{
		startGame();
		spell = SpellID(SpellID::decode(arrowKey)).toSpell();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell->getId());
		attackerSideHero->mana = 100;
		startBattle();
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		beginCombat();
	}

	void configure(spells::BattleCast & cast, std::optional<int64_t> value)
	{
		cast.setSpellLevel(0);
		cast.setEffectPower(24);
		if(value.has_value())
			cast.setEffectValue(*value);
	}

	int64_t predict(ControlledCaster & caster, std::optional<int64_t> value = std::nullopt,
		BattleInfo * source = nullptr, const Environment::GameCb * world = nullptr)
	{
		auto * selected = source ? source : battle();
		auto callback = std::make_shared<CPlayerBattleCallback>(selected, PlayerColor(0));
		DamageEnvironment environment(gameState(), world, selected);
		HypotheticBattle predicted(&environment, callback);
		const auto before = predicted.battleGetUnitByID(target->unitId())->getAvailableHealth();
		// A proxy is not a CGHeroInstance. HERO mode requires the real hero
		// for authoritative mana handling; these controlled effect casts are passive.
		spells::BattleCast cast(&predicted, &caster, spells::Mode::PASSIVE, spell);
		configure(cast, value);
		spells::Target destination;
		// Resolve the destination in the selected prediction, not the original
		// world's stack when this is a separately deserialized incoming battle.
		destination.emplace_back(predicted.battleGetUnitByID(target->unitId()));
		cast.castEval(predicted.getServerCallback(), destination);
		return before - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth();
	}

	void inspectIncoming(BattleInfo & incoming, const Environment::GameCb * world, int divisor, int64_t expectedRaw, bool initialized)
	{
		const auto * local = incoming.battleGetStackByID(target->unitId(), false);
		ASSERT_NE(local, nullptr);
		if(initialized)
		{
			EXPECT_EQ(local->getBattle(), &incoming) << "Incoming stack lifecycle binding";
			EXPECT_EQ(local->getMaxHealth(), target->getMaxHealth()) << "Incoming bonus graph attachment";
			EXPECT_EQ(local->getAvailableHealth(), target->getAvailableHealth()) << "Incoming health before AI";
			EXPECT_TRUE(local->isValidTarget(false));
		}
		DamageEnvironment environment(gameState(), world, &incoming);
		EXPECT_EQ(environment.battle(BattleID(0)), static_cast<const Environment::BattleCb *>(&incoming))
			<< "Environment and incoming callback must describe the same battle";
		ControlledCaster probe(attackerSideHero);
		probe.divisor = divisor;
		spells::BattleCast cast(&incoming, &probe, spells::Mode::PASSIVE, spell);
		configure(cast, std::nullopt);
		auto mechanics = spell->battleMechanics(&cast);
		EXPECT_EQ(mechanics->getEffectValue(), expectedRaw) << "Separate formula decoding from target/lifecycle";
		spells::Target localDestination;
		localDestination.emplace_back(local);
		std::cout << "Incoming lifecycle " << (initialized ? "after" : "before")
			<< " alive=" << local->alive() << " hp=" << local->getAvailableHealth()
			<< " maxHP=" << local->getMaxHealth() << " raw=" << mechanics->getEffectValue()
			<< " initializedBattleBound=" << (initialized && local->getBattle() == &incoming) << '\n';
		if(initialized)
			EXPECT_TRUE(mechanics->canBeCastAt(localDestination));
	}

	int64_t apply(ControlledCaster & caster, std::optional<int64_t> value = std::nullopt)
	{
		const auto before = target->getAvailableHealth();
		spells::BattleCast cast(battle(), &caster, spells::Mode::PASSIVE, spell);
		configure(cast, value);
		spells::Target destination;
		destination.emplace_back(target);
		cast.cast(gameHandler->spellEnv.get(), destination);
		return before - target->getAvailableHealth();
	}
};

TEST_F(NewHorizonsDirectDamageMechanicsTest, RealHeroLegalityAiPredictionAndAuthoritativeActionAgree)
{
	forceRealHeroScale = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 24, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);
	ASSERT_EQ(attackerSideHero->getEffectPower(spell), 24);
	ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	spells::Target destination;
	destination.emplace_back(target);
	spells::BattleCast legal(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto mechanics = spell->battleMechanics(&legal);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(destination, problem));
	ASSERT_EQ(mechanics->getEffectValue(), 68);
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->mana;
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	prediction.castEval(predicted.getServerCallback(), destination);
	EXPECT_EQ(before - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(), 68);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->mana, mana);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), 68);
	EXPECT_LT(attackerSideHero->mana, mana);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), 68) << "Rejected second hero action must not apply damage";
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, ActualAiPredictionAndServerApplicationUseSavedFormula)
{
	prepare();
	ControlledCaster caster(attackerSideHero);
	const auto before = target->getAvailableHealth();
	EXPECT_EQ(predict(caster), 68);
	EXPECT_EQ(target->getAvailableHealth(), before) << "Prediction must not mutate the real target";
	EXPECT_EQ(caster.valueReads, 1);
	EXPECT_EQ(apply(caster), 68);
	EXPECT_EQ(caster.valueReads, 2) << "Read the legacy caster override once per cast";
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, ExplicitEventZeroShortCircuitsCasterAndFormula)
{
	prepare();
	ControlledCaster caster(attackerSideHero);
	caster.overrideValue = 123;
	caster.divisor = 0; // Would reject formula evaluation if the override were ignored.
	EXPECT_EQ(predict(caster, 0), 0);
	EXPECT_EQ(apply(caster, 0), 0);
	EXPECT_EQ(caster.valueReads, 0);
	EXPECT_EQ(caster.divisorReads, 0);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, ExplicitEventValueWinsOverNonzeroCaster)
{
	prepare();
	ControlledCaster caster(attackerSideHero);
	caster.overrideValue = 123;
	caster.divisor = 0;
	EXPECT_EQ(predict(caster, 17), 17);
	EXPECT_EQ(apply(caster, 17), 17);
	EXPECT_EQ(caster.valueReads, 0);
	EXPECT_EQ(caster.divisorReads, 0);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, NonzeroCasterWinsOverFormulaAndIsReadOnce)
{
	prepare();
	ControlledCaster caster(attackerSideHero);
	caster.overrideValue = 123;
	caster.divisor = 0;
	EXPECT_EQ(predict(caster), 123);
	EXPECT_EQ(apply(caster), 123);
	EXPECT_EQ(caster.valueReads, 2);
	EXPECT_EQ(caster.divisorReads, 0);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, SavedZeroFormulaIsNotAbsence)
{
	authoredRules = savedFormula(0, 0);
	prepare();
	ControlledCaster caster(attackerSideHero);
	EXPECT_EQ(predict(caster), 0);
	EXPECT_EQ(apply(caster), 0);
	EXPECT_EQ(caster.valueReads, 2);
	EXPECT_GT(caster.divisorReads, 0);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, ExistingNegativeEventClampRemainsZero)
{
	prepare();
	ControlledCaster caster(attackerSideHero);
	EXPECT_EQ(predict(caster, -5), 0);
	EXPECT_EQ(apply(caster, -5), 0);
	EXPECT_EQ(caster.valueReads, 0);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, ExistingNegativeCasterClampRemainsZero)
{
	prepare();
	ControlledCaster caster(attackerSideHero);
	caster.overrideValue = -5;
	EXPECT_EQ(predict(caster), 0);
	EXPECT_EQ(apply(caster), 0);
	EXPECT_EQ(caster.valueReads, 2);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, SavedFormulaAlsoHonorsLegacyUnitDivisorOne)
{
	prepare();
	ControlledCaster caster(attackerSideHero);
	caster.divisor = 1;
	EXPECT_EQ(predict(caster), 500);
	EXPECT_EQ(apply(caster), 500);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, AbsentBattleFormulaRetainsOriginalRawDamage)
{
	savedEnabled = false;
	prepare();
	ControlledCaster caster(attackerSideHero);
	caster.divisor = 1;
	const auto original = spell->calculateRawEffectValue(0, 24, 1, 1);
	EXPECT_NE(original, 500);
	EXPECT_EQ(predict(caster), original);
	EXPECT_EQ(apply(caster), original);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, IncomingAbsentBattleCannotAdoptWorldFormula)
{
	savedEnabled = false;
	prepare();
	ConflictingMagicWorld otherWorld(*gameState(), savedFormula(777, 0));
	CMemorySerializer wire;
	wire.oser & *battle();
	wire.iser.cb = &otherWorld;
	ControlledCaster caster(attackerSideHero);
	caster.divisor = 1;
	const auto original = spell->calculateRawEffectValue(0, 24, 1, 1);
	ASSERT_NE(original, 777);
	{
		RestoreArmyBattleLinks restore(*attackerSideHero, *defenderSideHero);
		BattleInfo incoming(&otherWorld);
		ASSERT_EQ(newHorizonsMagic::directDamageValue(incoming.getMagicRules(), arrowKey, 24, 1), 777);
		wire.iser & incoming;
		ASSERT_FALSE(newHorizonsMagic::spellDirectDamage(incoming.getMagicRules(), arrowKey));
		inspectIncoming(incoming, &otherWorld, 1, original, false);
		incoming.localInit(); // Same post-wire lifecycle as the BattleStart consumer.
		inspectIncoming(incoming, &otherWorld, 1, original, true);
		EXPECT_EQ(predict(caster, std::nullopt, &incoming, &otherWorld), original);
	}
	ASSERT_EQ(attackerSideHero->battle, battle());
	ASSERT_EQ(defenderSideHero->battle, battle());
	EXPECT_EQ(apply(caster), original);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, IncomingSavedBattleBeatsConflictingWorldInRealAiEvaluation)
{
	prepare();
	ConflictingMagicWorld otherWorld(*gameState(), savedFormula(777, 0));
	CMemorySerializer wire;
	wire.oser & *battle();
	wire.iser.cb = &otherWorld;
	ControlledCaster caster(attackerSideHero);
	{
		RestoreArmyBattleLinks restore(*attackerSideHero, *defenderSideHero);
		BattleInfo incoming(&otherWorld);
		ASSERT_EQ(newHorizonsMagic::directDamageValue(incoming.getMagicRules(), arrowKey, 24, 10), 777);
		wire.iser & incoming;
		ASSERT_EQ(newHorizonsMagic::directDamageValue(otherWorld.getMagicRules(), arrowKey, 24, 10), 777);
		ASSERT_EQ(newHorizonsMagic::directDamageValue(incoming.getMagicRules(), arrowKey, 24, 10), 68);
		inspectIncoming(incoming, &otherWorld, 10, 68, false);
		incoming.localInit();
		inspectIncoming(incoming, &otherWorld, 10, 68, true);
		EXPECT_EQ(predict(caster, std::nullopt, &incoming, &otherWorld), 68);
	}
	ASSERT_EQ(attackerSideHero->battle, battle());
	ASSERT_EQ(defenderSideHero->battle, battle());
	EXPECT_EQ(apply(caster), 68);
	// Adversarial delegated-world callback + actual BattleInfo wire and AI effect
	// evaluation, not a full CGameState save-load or new-spell availability gate.
}
