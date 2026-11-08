/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../battleAI/ProjectedTargetHitFixture.h"
#include "../../luascript/StdInc.h"
#include "../../luascript/LuaContext.h"
#include "../../luascript/LuaModule.h"
#include "../../luascript/LuaScriptInstance.h"
#include "../../lib/combatScripts/NativeDamageCalculator.h"
#include "../../lib/bonuses/BonusFilter.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/scripting/ScriptHandler.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/CRandomGenerator.h"
#include <array>

namespace
{
using Values = std::array<int64_t, 6>;
Values values(const DamageEstimation & result)
{
	return {result.damage.min, result.damage.max, result.kills.min, result.kills.max,
		result.damageBeforeDefense.min, result.damageBeforeDefense.max};
}

/// Keep the production payload pipeline; replace only its final calculator.
/// Both sides read actual projected units/bonus graphs and the real callback.
class CaptureOracle final : public IDamageCalculatorScript
{
	scripting::LuaContext & lua;
	std::map<BonusType, std::string> declared;
	NativeDamageCalculator native;
	std::unordered_map<std::string, bool> presence(const battle::Unit & unit) const
	{
		std::unordered_map<std::string, bool> result;
		static const auto query = BonusFilter{}.compile();
		const auto bonuses = unit.getAllBonuses(query.first, query.second);
		for(const auto & bonus : *bonuses)
			if(const auto found = declared.find(bonus->type); found != declared.end()) result[found->second] = true;
		return result;
	}
public:
	bool returnNative = false;
	mutable size_t comparisons = 0;
	mutable std::set<int> capturedCaps;
	mutable bool sawHeroReduction = false;
	explicit CaptureOracle(scripting::LuaContext & lua) : lua(lua)
	{
		for(const auto & name : lua.callMethod<std::vector<std::string>>("bonusTypes", JsonNode()))
			declared.emplace(static_cast<BonusType>(BonusTypeID::decode(name)), name);
	}
	DamageEstimation calculate(const CBattleInfoCallback & battle, DamageAttackInfo & payload) const override
	{
		auto legacy = payload;
		legacy.attackerBonuses = presence(*payload.attacker);
		legacy.defenderBonuses = presence(*payload.defender);
		const auto answer = lua.callMethod<DamageEstimationPayload>("calculate", JsonNode(), &battle, legacy);
		DamageEstimation expected{};
		expected.damage = {answer.damage.min, answer.damage.max};
		expected.kills = {answer.kills.min, answer.kills.max};
		expected.damageBeforeDefense = {answer.damageBeforeDefense.min, answer.damageBeforeDefense.max};
		const auto actual = native.calculate(battle, payload);
		EXPECT_EQ(values(actual), values(expected)) << "capture " << comparisons << " shooting=" << payload.shooting
			<< " physical=" << payload.physicalDamage << " cap=" << payload.physicalDamageReductionCapPercent
			<< " attacker=" << payload.attacker->creatureId().getNum() << " lucky=" << payload.luckyStrike;
		++comparisons;
		capturedCaps.insert(payload.physicalDamageReductionCapPercent);
		sawHeroReduction |= payload.newHorizonsArmorerReductionPercent > 0;
		return returnNative ? actual : expected;
	}
	DamageForecastImplementation forecastImplementation(const CBattleInfoCallback &) const override
	{ return DamageForecastImplementation::CANONICAL; }
};

class OracleFactory final : public IScriptFactory
{
	std::shared_ptr<IDamageCalculatorScript> oracle;
public:
	explicit OracleFactory(std::shared_ptr<IDamageCalculatorScript> oracle) : oracle(std::move(oracle)) {}
	void initialize(const ScriptTypeDescription &) override {}
	std::shared_ptr<IDamageCalculatorScript> createDamageCalculatorScript(const std::string &) const override { return oracle; }
	std::shared_ptr<ICombatEventScript> createCombatEventScript(const std::string &) const override { return {}; }
	std::shared_ptr<spells::effects::Effect> createSpellEffect(const std::string &) const override { return {}; }
};

/// Serial fixture registry swap restores both exact original owners on every exit.
class OracleRegistry
{
	GameLibrary * library = LIBRARY;
	std::unique_ptr<CIdentifierStorage> identifiers;
	std::unique_ptr<ScriptHandler> scripts;
public:
	explicit OracleRegistry(const std::shared_ptr<CaptureOracle> & oracle)
	{
		auto clone = std::make_unique<CIdentifierStorage>(*library->identifiersHandler);
		identifiers = std::move(library->identifiersHandler);
		scripts = std::move(library->scriptTypeHandler);
		library->identifiersHandler = std::move(clone);
		library->scriptTypeHandler = std::make_unique<ScriptHandler>();
		library->scriptTypeHandler->registerFactory(std::make_shared<OracleFactory>(oracle));
		auto queryIdentifiers = std::move(library->identifiersHandler);
		auto restore = vstd::makeScopeGuard([&] { library->identifiersHandler = std::move(queryIdentifiers); });
		library->identifiersHandler = std::make_unique<CIdentifierStorage>();
		JsonNode descriptor;
		descriptor["script"].String() = "nativeDamageCaptureOracle";
		descriptor["implements"].String() = "damageCalculator";
		descriptor["priority"].Integer() = 0;
		library->scriptTypeHandler->loadObject("core", "nativeDamageCaptureOracle", descriptor);
	}
	~OracleRegistry()
	{
		library->scriptTypeHandler = std::move(scripts);
		library->identifiersHandler = std::move(identifiers);
	}
};

std::vector<JsonNode> snapshot(const HypotheticBattle & branch)
{
	std::vector<JsonNode> result;
	for(const auto * unit : branch.battleGetAllUnits(false))
	{
		result.push_back(unit->acquire()->save());
		const auto bonuses = unit->getUnstackedBonuses(Selector::all);
		for(const auto & bonus : *bonuses) result.push_back(bonus->toJsonNode());
	}
	return result;
}

void decorate(StackWithBonuses & attacker, StackWithBonuses & defender, int variation)
{
	if(variation == 0) return;
	const auto bonus = [](BonusType type, int value, BonusSubtypeID subtype = {})
	{ return Bonus(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, value, BonusSourceID(), subtype); };
	auto slayer = bonus(BonusType::SLAYER, 8);
	slayer.parameters = std::make_shared<BonusParameters>(3);
	attacker.addUnitBonus({bonus(BonusType::GENERAL_DAMAGE_PREMY, 17), bonus(BonusType::REVENGE, 1),
		bonus(BonusType::JOUSTING, 5), bonus(BonusType::ENEMY_DEFENCE_REDUCTION, 15),
		bonus(BonusType::PERCENTAGE_DAMAGE_BOOST, 14, BonusCustomSubtype::damageTypeMelee),
		bonus(BonusType::PERCENTAGE_DAMAGE_BOOST, 23, BonusCustomSubtype::damageTypeRanged),
		bonus(BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE, 11), bonus(BonusType::GENERAL_ATTACK_REDUCTION, 9), slayer});
	defender.addUnitBonus({bonus(BonusType::KING, 2), bonus(BonusType::VULNERABLE_FROM_BACK, 35),
		bonus(BonusType::ENEMY_ATTACK_REDUCTION, 13), bonus(BonusType::ENEMY_DEFENCE_REDUCTION, 7)});
	std::vector<Bonus> reductions;
	for(int group = 0; group < 3; ++group)
	{
		Bonus reduction(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
			group == 2 ? BonusSource::SPELL_EFFECT : BonusSource::CREATURE_ABILITY, 11 + group * 3,
			group == 2 ? BonusSourceID(SpellID(SpellID::SHIELD)) : BonusSourceID(group == 0 ? attacker.creatureId() : defender.creatureId()),
			BonusSubtypeID(BonusCustomSubtype::damageTypeAll));
		reduction.effectRange = group == 0 ? BonusLimitEffect::ONLY_MELEE_FIGHT : BonusLimitEffect::NO_LIMIT;
		reductions.push_back(reduction);
		reduction.subtype = BonusCustomSubtype::damageTypeRanged; reduction.val += 2;
		reductions.push_back(reduction);
	}
	auto basis = bonus(BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, 1250);
	basis.effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
	reductions.push_back(basis);
	defender.addUnitBonus(reductions);
	int64_t wound = attacker.getMaxHealth() / 2 + 1;
	attacker.damage(wound);
	if(variation == 2)
	{
		attacker.addUnitBonus({bonus(BonusType::IN_FRENZY, 100), bonus(BonusType::ALWAYS_MAXIMUM_DAMAGE, 2), bonus(BonusType::FORGETFULL, 1)});
		defender.addUnitBonus({bonus(BonusType::IN_FRENZY, 100), bonus(BonusType::DAMAGE_RECEIVED_CAP, 75)});
	}
}

class NativeDamageCaptureParityTest : public ProjectedTargetHitFixture
{
protected:
	virtual int cap() const { return 80; }
	void mapLoaded(CMap * map) override
	{
		ProjectedTargetHitFixture::mapLoaded(map);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		if(cap() < 0) magic.Struct().erase("physicalDamageReductionCapPercent");
		else magic["physicalDamageReductionCapPercent"].Integer() = cap();
		magic.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}
	void compare()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		ASSERT_NO_FATAL_FAILURE(selectArmorer(defenderSideHero, MasteryLevel::BASIC, "new-horizons:armorer.pavise"));
		ASSERT_NO_FATAL_FAILURE(selectArmorer(defenderSideHero, MasteryLevel::ADVANCED, "new-horizons:armorer.veteran"));
		ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::BASIC, "new-horizons:armorer.pavise"));
		ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::ADVANCED, "new-horizons:armorer.veteran"));
		std::vector<uint32_t> actors{braced->unitId()};
		for(const auto & name : {"core:monk", "core:champion", "core:magicElemental", "core:psychicElemental", "core:ballista"})
			actors.push_back(addStack(BattleSide::ATTACKER, creatureByName(name), BattleHex(35 + actors.size() * 3), 12)->unitId());
		resetChild();
		scripting::LuaModule host;
		std::vector<ScriptSource> patches;
		for(const auto & name : {"physicalProtection", "siegeWeapon", "magicElemental", "psychicElemental", "vulnerableFromBack",
			"hatesTrait", "revenge", "enemyAttackReduction", "damageReceivedCap"}) patches.emplace_back("core", "damage/" + std::string(name));
		scripting::LuaScriptInstance script(host, "core", ScriptPath::builtinTODO("damage/damageCalculator").addPrefix("SCRIPTS/"), patches);
		scripting::LuaContext context(&script, environment.get());
		context.initialize();
		auto oracle = std::make_shared<CaptureOracle>(context);
		const auto nativeState = gameState()->saveToMemory();
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
		OracleRegistry registry(oracle);
		for(const auto actorId : actors)
			for(int variation = 0; variation < 3; ++variation)
			{
				SCOPED_TRACE(::testing::Message() << "actor=" << actorId << " variation=" << variation);
				std::vector<Values> baseline;
				for(bool useNative : {false, true})
				{
					auto branch = std::make_shared<HypotheticBattle>(environment.get(), child);
					auto actor = branch->getForUpdate(actorId), target = branch->getForUpdate(incoming->unitId());
					decorate(*actor, *target, variation);
					const auto before = snapshot(*branch);
					oracle->returnNative = useNative;
					std::vector<Values> results;
					for(bool shooting : {false, true}) for(bool physical : {false, true}) for(int luck = 0; luck < 3; ++luck)
					{
						BattleAttackInfo attack(actor.get(), target.get(), 4, shooting);
						attack.physicalDamage = physical; attack.luckyStrike = luck == 1; attack.unluckyStrike = luck == 2;
						attack.deathBlow = variation == 2; attack.doubleDamage = variation == 1;
						attack.attackerPos = BattleHex(shooting ? 86 : 94); attack.defenderPos = BattleHex(100);
						results.push_back(values(branch->calculateDmgRange(attack)));
						std::swap(attack.attackerPos, attack.defenderPos);
						results.push_back(values(branch->calculateDmgRange(attack)));
					}
					// This callback conceals the enemy hero. Reverse the actor/recipient
					// once as well, so a visible own-side Armorer contribution is proven.
					results.push_back(values(branch->calculateDmgRange(BattleAttackInfo(target.get(), actor.get(), 0, false))));
					EXPECT_EQ(snapshot(*branch), before);
					if(useNative) EXPECT_EQ(results, baseline); else baseline = std::move(results);
				}
			}
		EXPECT_EQ(oracle->comparisons, actors.size() * 3 * 2 * (2 * 2 * 3 * 2 + 1));
		EXPECT_EQ(oracle->capturedCaps, std::set<int>{cap()});
		EXPECT_TRUE(oracle->sawHeroReduction);
		EXPECT_EQ(gameState()->saveToMemory(), nativeState);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
	}
};
class NativeLegacyDamageCaptureParityTest : public NativeDamageCaptureParityTest
{ int cap() const override { return -1; } };
class NativeZeroCapDamageCaptureParityTest : public NativeDamageCaptureParityTest
{ int cap() const override { return 0; } };
}

TEST_F(NativeDamageCaptureParityTest, FullLiveCaptureMatchesLuaWithCurrentProtection)
{ ASSERT_NO_FATAL_FAILURE(compare()); }
TEST_F(NativeLegacyDamageCaptureParityTest, FullLiveCaptureMatchesLuaWithHistoricalProtection)
{ ASSERT_NO_FATAL_FAILURE(compare()); }
TEST_F(NativeZeroCapDamageCaptureParityTest, FullLiveCaptureMatchesLuaWithExplicitZeroCap)
{ ASSERT_NO_FATAL_FAILURE(compare()); }
