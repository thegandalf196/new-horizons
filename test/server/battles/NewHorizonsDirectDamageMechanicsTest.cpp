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
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/battle/Destination.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/constants/StringConstants.h"
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

TEST(NewHorizonsHavocDirectDamage, CanonicalLevelOneRosterUsesSavedV2FormulasAndFiveMana)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_EQ(rules["rulesetVersion"].Integer(), 2);
	struct Expected
	{
		const char * id;
		int32_t base;
		int32_t coefficient;
	};
	for(const auto & expected : std::vector<Expected>{
		{"core:fireball", 25, 8},
		{"core:iceBolt", 45, 10},
		{"core:lightningBolt", 20, 15}})
	{
		const auto & record = rules["spells"][expected.id];
		ASSERT_EQ(record["level"].Integer(), 1) << expected.id;
		ASSERT_EQ(record["schools"].Vector().size(), 1u) << expected.id;
		EXPECT_EQ(record["schools"].Vector().front().String(), "new-horizons:havoc") << expected.id;
		ASSERT_EQ(record["costs"].Vector().size(), 4u) << expected.id;
		for(const auto & cost : record["costs"].Vector())
			EXPECT_EQ(cost.Integer(), 5) << expected.id;
		EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, expected.id, 20, 10),
			expected.base + expected.coefficient * 2) << expected.id;
	}

	EXPECT_GT(newHorizonsMagic::directDamageValue(rules, "core:iceBolt", 20, 10),
		newHorizonsMagic::directDamageValue(rules, "core:lightningBolt", 20, 10));
	EXPECT_GT(newHorizonsMagic::directDamageValue(rules, "core:lightningBolt", 100, 10),
		newHorizonsMagic::directDamageValue(rules, "core:iceBolt", 100, 10));
}

TEST(NewHorizonsHavocDirectDamage, CanonicalFrostRingAndInfernoUseDetailedRosterValues)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	struct Expected
	{
		const char * id;
		int32_t level;
		int32_t cost;
		int32_t base;
		int32_t coefficient;
	};
	for(const auto & expected : std::vector<Expected>{
		{"core:frostRing", 2, 8, 55, 11},
		{"core:inferno", 3, 13, 70, 12}})
	{
		const auto & record = rules["spells"][expected.id];
		EXPECT_EQ(record["level"].Integer(), expected.level) << expected.id;
		ASSERT_EQ(record["costs"].Vector().size(), 4u) << expected.id;
		for(const auto & cost : record["costs"].Vector())
			EXPECT_EQ(cost.Integer(), expected.cost) << expected.id;
		EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, expected.id, 20, 10),
			expected.base + expected.coefficient * 2) << expected.id;
	}
}

TEST(NewHorizonsHavocDirectDamage, CanonicalLandMineUsesDetailedRosterValues)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	const auto & record = rules["spells"]["core:landMine"];
	EXPECT_EQ(record["level"].Integer(), 2);
	ASSERT_EQ(record["costs"].Vector().size(), 4u);
	for(const auto & cost : record["costs"].Vector())
		EXPECT_EQ(cost.Integer(), 8);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, "core:landMine", 0, 10), 60);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, "core:landMine", 100, 10), 170);
}

TEST(NewHorizonsHavocDirectDamage, CanonicalFireWallUsesDetailedRosterValues)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	const auto & record = rules["spells"]["core:fireWall"];
	EXPECT_EQ(record["level"].Integer(), 3);
	ASSERT_EQ(record["costs"].Vector().size(), 4u);
	for(const auto & cost : record["costs"].Vector())
		EXPECT_EQ(cost.Integer(), 12);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, "core:fireWall", 0, 10), 40);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, "core:fireWall", 100, 10), 140);
}

TEST(NewHorizonsHavocDirectDamage, CanonicalHighLevelSpellsUseDetailedRosterValues)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	struct Expected
	{
		const char * id;
		int64_t level;
		int64_t cost;
		int64_t powerZero;
		int64_t powerHundred;
	};
	for(const Expected expected : {
		Expected{"core:chainLightning", 4, 17, 130, 310},
		Expected{"core:meteorShower", 4, 18, 110, 260},
		Expected{"core:armageddon", 5, 24, 150, 330},
	})
	{
		const auto & record = rules["spells"][expected.id];
		EXPECT_EQ(record["level"].Integer(), expected.level);
		ASSERT_EQ(record["costs"].Vector().size(), 4u);
		for(const auto & cost : record["costs"].Vector())
			EXPECT_EQ(cost.Integer(), expected.cost);
		EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, expected.id, 0, 10), expected.powerZero);
		EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, expected.id, 100, 10), expected.powerHundred);
	}
}

class NewHorizonsDirectDamageMechanicsTest : public HeroCommandFixture
{
protected:
	bool savedEnabled = true;
	bool forceRealHeroScale = false;
	bool usePerks = false;
	JsonNode authoredRules;
	std::string selectedSpellKey = arrowKey;
	CStack * target = nullptr;
	const CSpell * spell = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
		authoredRules = savedFormula();
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(forceRealHeroScale)
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		if(usePerks)
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, savedEnabled ? authoredRules : JsonNode());
	}

	void prepare()
	{
		startGame();
		spell = SpellID(SpellID::decode(selectedSpellKey)).toSpell();
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

TEST_F(NewHorizonsDirectDamageMechanicsTest, IceBoltUsesCanonicalDamageAndFiveManaInAuthoritativeCast)
{
	forceRealHeroScale = true;
	selectedSpellKey = "core:iceBolt";
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getSpellCost(spell), 5);
	auto * adjacent = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex - 1), 1000);
	const auto healthBefore = target->getAvailableHealth();
	const auto adjacentBefore = adjacent->getAvailableHealth();
	const auto manaBefore = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), 65);
	EXPECT_EQ(adjacent->getAvailableHealth(), adjacentBefore);
	EXPECT_EQ(attackerSideHero->mana, manaBefore - 5);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, LightningBoltUsesCanonicalHighPowerDamageAndFiveMana)
{
	forceRealHeroScale = true;
	selectedSpellKey = "core:lightningBolt";
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getSpellCost(spell), 5);
	auto * adjacent = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex - 1), 1000);
	const auto healthBefore = target->getAvailableHealth();
	const auto adjacentBefore = adjacent->getAvailableHealth();
	const auto manaBefore = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), 170);
	EXPECT_EQ(adjacent->getAvailableHealth(), adjacentBefore);
	EXPECT_EQ(attackerSideHero->mana, manaBefore - 5);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, FireballAppliesCanonicalDamageToTargetAndAdjacentOnly)
{
	forceRealHeroScale = true;
	selectedSpellKey = "core:fireball";
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	auto * adjacent = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex - 1), 1000);
	auto * distant = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 2), 1000);
	ASSERT_TRUE(vstd::contains(BattleHexArray::getNeighbouringTiles(target->getPosition()), adjacent->getPosition()));
	ASSERT_FALSE(vstd::contains(BattleHexArray::getNeighbouringTiles(target->getPosition()), distant->getPosition()));
	const auto targetBefore = target->getAvailableHealth();
	const auto adjacentBefore = adjacent->getAvailableHealth();
	const auto distantBefore = distant->getAvailableHealth();
	const auto manaBefore = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(targetBefore - target->getAvailableHealth(), 41);
	EXPECT_EQ(adjacentBefore - adjacent->getAvailableHealth(), 41);
	EXPECT_EQ(distant->getAvailableHealth(), distantBefore);
	EXPECT_EQ(attackerSideHero->mana, manaBefore - 5);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, FrostRingLeavesCenterSafeAndDamagesOnlyTheSurroundingRing)
{
	forceRealHeroScale = true;
	selectedSpellKey = "core:frostRing";
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	std::vector<CStack *> ring;
	for(const auto hex : BattleHexArray::getNeighbouringTiles(target->getPosition()))
		ring.push_back(addStack(ring.size() % 2 == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER,
			creatureByName("core:pikeman"), hex, 1000));
	ASSERT_EQ(ring.size(), 6u);
	auto * distant = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 2), 1000);
	const auto centerBefore = target->getAvailableHealth();
	std::vector<int64_t> ringBefore;
	for(const auto * stack : ring)
		ringBefore.push_back(stack->getAvailableHealth());
	const auto distantBefore = distant->getAvailableHealth();
	const auto manaBefore = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToHex(target->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getAvailableHealth(), centerBefore);
	for(size_t index = 0; index < ring.size(); ++index)
		EXPECT_EQ(ringBefore[index] - ring[index]->getAvailableHealth(), 77) << "ring index " << index;
	EXPECT_EQ(distant->getAvailableHealth(), distantBefore);
	EXPECT_EQ(attackerSideHero->mana, manaBefore - 8);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, InfernoDamagesItsBroadRadiusWithCanonicalFormula)
{
	forceRealHeroScale = true;
	selectedSpellKey = "core:inferno";
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	auto * inner = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex - 1), 1000);
	auto * outer = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex - 2), 1000);
	auto * distant = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 3), 1000);
	const auto targetBefore = target->getAvailableHealth();
	const auto innerBefore = inner->getAvailableHealth();
	const auto outerBefore = outer->getAvailableHealth();
	const auto distantBefore = distant->getAvailableHealth();
	const auto manaBefore = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToHex(target->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(targetBefore - target->getAvailableHealth(), 94);
	EXPECT_EQ(innerBefore - inner->getAvailableHealth(), 94);
	EXPECT_EQ(outerBefore - outer->getAvailableHealth(), 94);
	EXPECT_EQ(distant->getAvailableHealth(), distantBefore);
	EXPECT_EQ(attackerSideHero->mana, manaBefore - 13);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, MagicArrowOverchargeUsesTheSamePredictionAndAuthoritativeManaPath)
{
	forceRealHeroScale = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);

	spells::Target destination;
	destination.emplace_back(target);
	spells::BattleCast legal(battle(), attackerSideHero, spells::Mode::HERO, spell);
	legal.setOvercharge(4);
	auto mechanics = spell->battleMechanics(&legal);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(destination, problem));
	EXPECT_EQ(mechanics->getEffectValue(), 352);

	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->mana;
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	prediction.setOvercharge(4);
	prediction.castEval(predicted.getServerCallback(), destination);
	EXPECT_EQ(before - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(), 352);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->mana, mana);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.spellOvercharge = 4;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), 352);
	EXPECT_EQ(attackerSideHero->mana, mana - 8);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, WisdomDiscountsMagicArrowBaseButNotOverchargeSurcharge)
{
	forceRealHeroScale = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const int wisdomId = SecondarySkill::decode("new-horizons:wisdom");
	ASSERT_GE(wisdomId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(wisdomId), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	// Magic Arrow is listed at four mana. Expert Wisdom discounts the base to
	// ceil(4 * .70) = 3; four selected overcharge points are then added in full.
	ASSERT_EQ(attackerSideHero->getListedSpellCost(spell), 4);
	ASSERT_EQ(attackerSideHero->getSpellCost(spell), 3);
	ASSERT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), 3);

	const auto mana = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.spellOvercharge = 4;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->mana, mana - 7);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, OverchargerExtendsPredictionAndAuthoritativeCastToSixPoints)
{
	forceRealHeroScale = true;
	usePerks = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 150, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection(
		{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.overcharger"});

	spells::Target destination;
	destination.emplace_back(target);
	spells::BattleCast tooMuch(battle(), attackerSideHero, spells::Mode::HERO, spell);
	tooMuch.setOvercharge(7);
	spells::detail::ProblemImpl excessProblem;
	EXPECT_FALSE(spell->battleMechanics(&tooMuch)->canBeCast(excessProblem));

	spells::BattleCast legal(battle(), attackerSideHero, spells::Mode::HERO, spell);
	legal.setOvercharge(6);
	auto mechanics = spell->battleMechanics(&legal);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(destination, problem));
	EXPECT_EQ(mechanics->getEffectValue(), 656);

	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->mana;
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	prediction.setOvercharge(6);
	prediction.castEval(predicted.getServerCallback(), destination);
	EXPECT_EQ(before - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(), 656);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->mana, mana);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.spellOvercharge = 6;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), 656);
	EXPECT_EQ(attackerSideHero->mana, mana - 10);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, ConductorUsesAuthoredPerJumpMultipliersInPredictionAndCast)
{
	forceRealHeroScale = true;
	usePerks = true;
	selectedSpellKey = "core:chainLightning";
	prepare();

	const auto havoc = SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic"));
	ASSERT_TRUE(havoc.hasValue());
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:havocMagic", "new-horizons:havocMagic.conductor"});

	std::vector<CStack *> chained{target};
	for(const auto hex : target->getSurroundingHexes())
	{
		if(chained.size() == 4)
			break;
		chained.push_back(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), hex, 1000));
	}
	ASSERT_EQ(chained.size(), 4u);
	std::vector<int64_t> healthBefore;
	for(const auto * stack : chained)
		healthBefore.push_back(stack->getAvailableHealth());

	ControlledCaster caster(attackerSideHero);
	const auto firstPredicted = predict(caster);
	const auto firstActual = apply(caster);
	EXPECT_EQ(firstActual, firstPredicted);

	std::vector<int64_t> damages;
	for(size_t index = 0; index < chained.size(); ++index)
	{
		const auto damage = healthBefore[index] - chained[index]->getAvailableHealth();
		if(damage > 0)
			damages.push_back(damage);
	}
	ASSERT_EQ(damages.size(), 4u);
	std::sort(damages.begin(), damages.end(), std::greater<int64_t>());
	const auto initial = damages.front();
	EXPECT_EQ(damages[1], initial * 75 / 100);
	EXPECT_EQ(damages[2], initial * 55 / 100);
	EXPECT_EQ(damages[3], initial * 40 / 100);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, ConductorNeverReplacesBetterLevelScaledMasterChainRetention)
{
	forceRealHeroScale = true;
	usePerks = true;
	selectedSpellKey = "new-horizons:masterChainLightning";
	prepare();

	const auto havoc = SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic"));
	ASSERT_TRUE(havoc.hasValue());
	attackerSideHero->level = 12;
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:havocMagic", "new-horizons:havocMagic.conductor"});

	std::vector<CStack *> chained{target};
	for(const auto hex : target->getSurroundingHexes())
	{
		if(chained.size() == 4)
			break;
		chained.push_back(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), hex, 1000));
	}
	ASSERT_EQ(chained.size(), 4u);
	std::vector<int64_t> healthBefore;
	for(const auto * stack : chained)
		healthBefore.push_back(stack->getAvailableHealth());

	ControlledCaster caster(attackerSideHero);
	EXPECT_EQ(apply(caster), predict(caster));

	std::vector<int64_t> damages;
	for(size_t index = 0; index < chained.size(); ++index)
	{
		const auto damage = healthBefore[index] - chained[index]->getAvailableHealth();
		if(damage > 0)
			damages.push_back(damage);
	}
	ASSERT_EQ(damages.size(), 4u);
	std::sort(damages.begin(), damages.end(), std::greater<int64_t>());
	const auto initial = damages.front();
	EXPECT_EQ(damages[1], initial * 87 / 100);
	EXPECT_EQ(damages[2], initial * 7569 / 10000);
	EXPECT_EQ(damages[3], initial * 658503 / 1000000);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, AnnihilatorIgnoresTwentyPercentMagicalReductionAndMatchesPrediction)
{
	forceRealHeroScale = true;
	usePerks = true;
	selectedSpellKey = "new-horizons:disintegrate";
	prepare();

	const auto havoc = SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic"));
	ASSERT_TRUE(havoc.hasValue());
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	const auto reduction = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::CREATURE_ABILITY, 50,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
	target->addNewBonus(reduction);

	ControlledCaster caster(attackerSideHero);
	const auto baseline = apply(caster);
	EXPECT_EQ(baseline, 120);

	auto * annihilatorTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
		BattleHex(rightHex - 1), 1000);
	annihilatorTarget->addNewBonus(std::make_shared<Bonus>(*reduction));
	attackerSideHero->applyPerkSelection({
		"new-horizons:havocMagic", "new-horizons:havocMagic.annihilator"});
	target = annihilatorTarget;

	const auto predicted = predict(caster);
	const auto actual = apply(caster);
	EXPECT_EQ(predicted, actual);
	EXPECT_EQ(actual, 144);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, TemporalistExtendsOnlyOrdinaryHeroSlowDuration)
{
	forceRealHeroScale = true;
	usePerks = true;
	prepare();
	const auto * slow = SpellID(SpellID::SLOW).toSpell();
	attackerSideHero->addSpellToSpellbook(slow->getId());
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection(
		{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalist"});
	const int ordinaryDuration = attackerSideHero->getEnchantPower(slow);

	spells::BattleCast ordinary(battle(), attackerSideHero, spells::Mode::HERO, slow);
	EXPECT_EQ(slow->battleMechanics(&ordinary)->getEffectDuration(), ordinaryDuration + 1);

	spells::BattleCast explicitDuration(battle(), attackerSideHero, spells::Mode::HERO, slow);
	explicitDuration.setEffectDuration(ordinaryDuration + 7);
	EXPECT_EQ(slow->battleMechanics(&explicitDuration)->getEffectDuration(), ordinaryDuration + 7);

	ControlledCaster nonHero(attackerSideHero);
	spells::BattleCast passive(battle(), &nonHero, spells::Mode::PASSIVE, slow);
	EXPECT_EQ(slow->battleMechanics(&passive)->getEffectDuration(), ordinaryDuration);

	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 0, ChangeValueMode::ABSOLUTE);
	spells::BattleCast rankLost(battle(), attackerSideHero, spells::Mode::HERO, slow);
	EXPECT_EQ(slow->battleMechanics(&rankLost)->getEffectDuration(), ordinaryDuration);
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 1, ChangeValueMode::ABSOLUTE);

	spells::BattleCast otherSpell(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&otherSpell)->getEffectDuration(), attackerSideHero->getEnchantPower(spell));

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = slow->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto slowBonuses = target->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW))));
	ASSERT_FALSE(slowBonuses->empty());
	EXPECT_EQ(slowBonuses->front()->turnsRemain, ordinaryDuration + 1);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, PlannedTemporalistInAnOlderSnapshotStaysInactive)
{
	forceRealHeroScale = true;
	usePerks = true;
	prepare();
	const auto * slow = SpellID(SpellID::SLOW).toSpell();
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 1, ChangeValueMode::ABSOLUTE);
	auto & saved = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	for(auto & perk : saved.rules["skills"]["new-horizons:sorceryMagic"]["perks"].Vector())
		if(perk["id"].String() == "new-horizons:sorceryMagic.temporalist")
			perk["effect"]["status"].String() = "planned";
	attackerSideHero->applyPerkSelection(
		{"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalist"});

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, slow);
	EXPECT_EQ(slow->battleMechanics(&cast)->getEffectDuration(), attackerSideHero->getEnchantPower(slow));
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, MagicArrowRejectsOutOfRangeAndLegacyOverchargeAtomically)
{
	forceRealHeroScale = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->mana;

	BattleAction tooMuch;
	tooMuch.actionType = EActionType::HERO_SPELL;
	tooMuch.side = BattleSide::ATTACKER;
	tooMuch.spell = spell->getId();
	tooMuch.spellOvercharge = 5; // SP 100 permits only four.
	tooMuch.aimToUnit(target);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), tooMuch));
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->mana, mana);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, MagicArrowRejectsMissingTargetBeforeSpendingManaOrAction)
{
	forceRealHeroScale = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->mana;

	BattleAction missingTarget;
	missingTarget.actionType = EActionType::HERO_SPELL;
	missingTarget.side = BattleSide::ATTACKER;
	missingTarget.spell = spell->getId();
	missingTarget.spellOvercharge = 4;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), missingTarget));
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->mana, mana);

	BattleAction valid = missingTarget;
	valid.aimToUnit(target);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), valid))
		<< "Rejected target must leave the shared hero action available";
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, LegacyMagicArrowRejectsOverchargeWithoutMutation)
{
	savedEnabled = false;
	prepare();
	const auto legacyHealth = target->getAvailableHealth();
	const auto legacyMana = attackerSideHero->mana;
	BattleAction legacy;
	legacy.actionType = EActionType::HERO_SPELL;
	legacy.side = BattleSide::ATTACKER;
	legacy.spell = spell->getId();
	legacy.spellOvercharge = 1;
	legacy.aimToUnit(target);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), legacy));
	EXPECT_EQ(target->getAvailableHealth(), legacyHealth);
	EXPECT_EQ(attackerSideHero->mana, legacyMana);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, LegacyWisdomDoesNotDiscountMagicArrow)
{
	savedEnabled = false;
	prepare();
	const int wisdomId = SecondarySkill::decode("new-horizons:wisdom");
	ASSERT_GE(wisdomId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(wisdomId), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	// The installed Wisdom skill must not retrofit New Horizons semantics into
	// a legacy saved world. Legacy Magic Arrow remains its original listed cost.
	const auto legacyListed = spell->getCost(attackerSideHero->getSpellSchoolLevel(spell));
	ASSERT_EQ(attackerSideHero->getListedSpellCost(spell), legacyListed);
	ASSERT_EQ(attackerSideHero->getSpellCost(spell), legacyListed);
	const auto mana = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->mana, mana - legacyListed);
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
