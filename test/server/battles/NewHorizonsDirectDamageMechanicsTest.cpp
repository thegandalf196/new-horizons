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
#include "../../../lib/battle/CUnitState.h"
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
#include "../../../lib/spells/effects/Effect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../server/ServerSpellCastEnvironment.h"
#include <vcmi/Environment.h>
#include <iostream>
#include <limits>

namespace
{
constexpr auto arrowKey = "core:magicArrow";
constexpr auto slowKey = "core:slow";
constexpr auto havocMagicKey = "new-horizons:havocMagic";
constexpr auto stormcallerPerkKey = "new-horizons:havocMagic.stormcaller";
constexpr auto pyromancerPerkKey = "new-horizons:havocMagic.pyromancer";
constexpr auto cryomancerPerkKey = "new-horizons:havocMagic.cryomancer";
constexpr auto controlledBlastPerkKey = "new-horizons:havocMagic.controlledBlast";

struct ControlledBlastSpellCase
{
	const char * spellId;
	const char * centerCreatureId;
	bool centerIsHypnotized;
	bool centerIsDoubleWide;
	int64_t flatBase;
	const char * name;
};

struct HavocDamagePerkCase
{
	const char * spellId;
	const char * perkId;
	int additionalCoefficientPercent;
	int flatBase;
	int powerCoefficient;
	int rawSpellPower;
	const char * name;
};

void activateFixturePerks(JsonNode & rules, const std::vector<std::string> & perkIds)
{
	for(auto & [skillId, skill] : rules["skills"].Struct())
	{
		(void)skillId;
		for(auto & perk : skill["perks"].Vector())
			if(vstd::contains(perkIds, perk["id"].String()))
				perk["effect"]["status"].String() = "active";
	}
}
JsonNode savedFormula(int base = 20, int coefficient = 20)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = 2;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	rules["spells"][arrowKey]["directDamage"]["base"].Integer() = base;
	rules["spells"][arrowKey]["directDamage"]["powerCoefficient"].Integer() = coefficient;
	return rules;
}

JsonNode savedV3Formula(int base = 20, int coefficient = 20)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["spells"][arrowKey]["directDamage"]["base"].Integer() = base;
	rules["spells"][arrowKey]["directDamage"]["powerCoefficient"].Integer() = coefficient;
	return rules;
}

JsonNode magicRulesForVersion(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(version == newHorizonsMagic::CURRENT_RULESET_VERSION)
		return rules;

	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [spellId, spell] : rules["spells"].Struct())
	{
		(void)spellId;
		spell.Struct().erase("selectedPlacement");
		spell.Struct().erase("earthquake");
		spell.Struct().erase("structures");
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	if(version == newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION)
	{
		newHorizonsMagic::validateRules(rules);
		return rules;
	}

	rules.Struct().erase("spellPoints");
	rules.Struct().erase("mageGuildGeneration");
	rules.Struct().erase("physicalDamageReductionCapPercent");
	rules.Struct().erase("warcasting");
	for(auto & [factionId, faction] : rules["factions"].Struct())
	{
		(void)factionId;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	for(auto & [spellId, spell] : rules["spells"].Struct())
	{
		(void)spellId;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
	for(auto it = rules["spells"].Struct().begin(); it != rules["spells"].Struct().end();)
	{
		if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
			it = rules["spells"].Struct().erase(it);
		else
			++it;
	}
	rules.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	newHorizonsMagic::validateRules(rules);
	return rules;
}

struct SlowRankCase
{
	int rank;
	int expectedCoefficientPercent;
	int expectedMagnitude;
	const char * name;
};

struct SlowLegacyCase
{
	int version;
	const char * name;
};

JsonNode savedV1MagicRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::RULESET_VERSION;
	rules.Struct().erase("spellPoints");
	rules.Struct().erase("mageGuildGeneration");
	rules.Struct().erase("physicalDamageReductionCapPercent");
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	rules.Struct().erase("warcasting");
	for(auto & [factionId, faction] : rules["factions"].Struct())
	{
		(void)factionId;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	for(auto & [spellId, spell] : rules["spells"].Struct())
	{
		(void)spellId;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
	for(auto it = rules["spells"].Struct().begin(); it != rules["spells"].Struct().end();)
	{
		if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
			it = rules["spells"].Struct().erase(it);
		else
			++it;
	}
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
	ASSERT_EQ(rules["rulesetVersion"].Integer(), 3);
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

TEST(NewHorizonsDirectDamage, RegenerationBasisPointMaximumInputsSaturateWithoutOverflow)
{
	EXPECT_EQ(newHorizonsMagic::regenerationRateMillionthsBasisPoints(
		std::numeric_limits<int32_t>::max(), 100'000, true, 1000, 1000),
		newHorizonsMagic::REGENERATION_MAX_RATE_MILLIONTHS);
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
	bool omitDefaultTarget = false;
	std::vector<std::string> fixtureActivePerkIds;
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
		{
			JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
			activateFixturePerks(perkRules, fixtureActivePerkIds);
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		}
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, savedEnabled ? authoredRules : JsonNode());
	}

	void prepare()
	{
		startGame();
		spell = SpellID(SpellID::decode(selectedSpellKey)).toSpell();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell->getId());
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		if(!omitDefaultTarget)
			target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		beginCombat();
	}

	void prepareControlledBlastSpell(const std::string & spellId)
	{
		forceRealHeroScale = true;
		usePerks = true;
		omitDefaultTarget = true;
		fixtureActivePerkIds = {controlledBlastPerkKey};
		selectedSpellKey = spellId;
		authoredRules = savedV3Formula();
		prepare();
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		const auto havoc = SecondarySkill(SecondarySkill::decode(havocMagicKey));
		ASSERT_TRUE(havoc.hasValue());
		attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	}

	void selectControlledBlast()
	{
		attackerSideHero->applyPerkSelection({havocMagicKey, stormcallerPerkKey});
		attackerSideHero->applyPerkSelection({havocMagicKey, controlledBlastPerkKey});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(havocMagicKey, stormcallerPerkKey));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(havocMagicKey, controlledBlastPerkKey));
	}

	std::map<uint32_t, int64_t> previewAreaDamageAtHex(const BattleHex & aimHex)
	{
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		DamageEnvironment environment(gameState(), nullptr);
		HypotheticBattle predicted(&environment, callback);
		spells::Target aim{spells::Destination(aimHex)};
		spells::BattleCast preview(&predicted, attackerSideHero, spells::Mode::HERO, spell);
		auto mechanics = spell->battleMechanics(&preview);
		spells::detail::ProblemImpl problem;
		EXPECT_TRUE(mechanics->canBeCast(problem));
		EXPECT_TRUE(mechanics->canBeCastAt(aim, problem));
		mechanics->castEval(predicted.getServerCallback(), aim);

		std::map<uint32_t, int64_t> damage;
		for(const auto * original : battle()->battleGetAllStacks())
		{
			const auto * projected = predicted.battleGetUnitByID(original->unitId());
			if(projected)
				damage.emplace(original->unitId(),
					original->getAvailableHealth() - projected->getAvailableHealth());
		}
		return damage;
	}

	void prepareSlow(int magicVersion, int sorceryRank, int spellPower, bool selectTemporalist = false)
	{
		forceRealHeroScale = true;
		usePerks = true;
		authoredRules = magicRulesForVersion(magicVersion);
		selectedSpellKey = slowKey;
		prepare();
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), magicVersion);
		const int sorceryMagic = SecondarySkill::decode("new-horizons:sorceryMagic");
		ASSERT_GE(sorceryMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(sorceryMagic), sorceryRank,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower,
			ChangeValueMode::ABSOLUTE);
		if(selectTemporalist)
			attackerSideHero->applyPerkSelection({"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalist"});
	}

	std::shared_ptr<const Bonus> slowBonus(const battle::Unit * unit) const
	{
		return unit->getBonus(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::SLOW))));
	}

	void selectSpellPenetration()
	{
		const auto spellcraftId = SecondarySkill::decode("new-horizons:spellcraft");
		ASSERT_GE(spellcraftId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(spellcraftId), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({
			"new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(
			"new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"));
	}

	void selectEmpowerSpell()
	{
		selectSpellPenetration();
		const auto spellcraft = SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft"));
		ASSERT_TRUE(spellcraft.hasValue());
		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({
			"new-horizons:spellcraft", "new-horizons:spellcraft.empowerSpell"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(
			"new-horizons:spellcraft", "new-horizons:spellcraft.empowerSpell"));
	}

	void verifyEmpowerSpellCostThreshold(int listedCost, int expectedWisdomCost,
		int64_t expectedDamage, int expectedEmpowerBonus)
	{
		forceRealHeroScale = true;
		usePerks = true;
		authoredRules = savedV3Formula();
		for(auto & cost : authoredRules["spells"][arrowKey]["costs"].Vector())
			cost.Integer() = listedCost;
		prepare();
		selectEmpowerSpell();

		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		const auto wisdom = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
		ASSERT_TRUE(wisdom.hasValue());
		attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->getListedSpellCost(spell), listedCost);
		ASSERT_EQ(attackerSideHero->getSpellCost(spell), expectedWisdomCost);
		EXPECT_EQ(newHorizonsMagic::empowerSpellBonusPercent(
			battle()->getMagicRules(), attackerSideHero, spell->getId()), expectedEmpowerBonus);

		spells::Target aim;
		aim.emplace_back(target);
		spells::BattleCast legal(battle(), attackerSideHero, spells::Mode::HERO, spell);
		auto mechanics = spell->battleMechanics(&legal);
		spells::detail::ProblemImpl problem;
		ASSERT_TRUE(mechanics->canBeCast(problem));
		ASSERT_TRUE(mechanics->canBeCastAt(aim, problem));
		EXPECT_EQ(mechanics->getEffectValue(), expectedDamage);
		EXPECT_EQ(spell->calculateDamage(attackerSideHero), expectedDamage)
			<< "CSpell's damage estimate should use the same eligible Empower bonus";

		const auto healthBefore = target->getAvailableHealth();
		const auto manaBefore = attackerSideHero->getManaAvailable();
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		DamageEnvironment environment(gameState(), nullptr);
		HypotheticBattle predicted(&environment, callback);
		const auto * projectedTarget = predicted.battleGetUnitByID(target->unitId());
		ASSERT_NE(projectedTarget, nullptr);
		spells::Target projectedAim;
		projectedAim.emplace_back(projectedTarget);
		spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
		auto predictedMechanics = spell->battleMechanics(&prediction);
		EXPECT_EQ(predictedMechanics->getEffectValue(), expectedDamage);
		predictedMechanics->castEval(predicted.getServerCallback(), projectedAim);
		EXPECT_EQ(healthBefore - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(),
			expectedDamage);
		EXPECT_EQ(target->getAvailableHealth(), healthBefore);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell->getId();
		action.aimToUnit(target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(healthBefore - target->getAvailableHealth(), expectedDamage);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - expectedWisdomCost);
	}

	void verifyIceBoltSavedProfile(bool legacySlow, int expectedVersion,
		std::optional<int64_t> expectedDamage = std::nullopt)
	{
		forceRealHeroScale = true;
		selectedSpellKey = "core:iceBolt";
		prepare();
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), expectedVersion);
		ASSERT_EQ(attackerSideHero->getSpellCost(spell), 5);

		auto * adjacent = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
			BattleHex(rightHex - 1), 1000);
		const auto targetHealthBefore = target->getAvailableHealth();
		const auto adjacentHealthBefore = adjacent->getAvailableHealth();
		const auto movementBefore = target->getMovementRange();
		const auto initiativeBefore = target->getInitiative();
		const auto manaBefore = attackerSideHero->getManaAvailable();
		ASSERT_GE(movementBefore, 2u);

		spells::Target aim;
		aim.emplace_back(target);
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		DamageEnvironment environment(gameState(), nullptr);
		HypotheticBattle predicted(&environment, callback);
		const auto * projectedTarget = predicted.battleGetUnitByID(target->unitId());
		ASSERT_NE(projectedTarget, nullptr);
		spells::Target previewAim;
		previewAim.emplace_back(projectedTarget);
		spells::BattleCast preview(&predicted, attackerSideHero, spells::Mode::HERO, spell);
		auto mechanics = spell->battleMechanics(&preview);
		const auto affected = mechanics->getAffectedStacks(previewAim);
		ASSERT_EQ(affected.size(), 1u);
		EXPECT_EQ(affected.front()->unitId(), target->unitId());
		mechanics->castEval(predicted.getServerCallback(), previewAim);

		projectedTarget = predicted.battleGetUnitByID(target->unitId());
		ASSERT_NE(projectedTarget, nullptr);
		const auto previewDamage = targetHealthBefore - projectedTarget->getAvailableHealth();
		EXPECT_GT(previewDamage, 0);
		if(expectedDamage)
		{
			EXPECT_EQ(previewDamage, *expectedDamage);
		}
		EXPECT_EQ(projectedTarget->getMovementRange(), movementBefore - (legacySlow ? 2u : 0u));
		EXPECT_EQ(projectedTarget->getInitiative(), initiativeBefore);
		EXPECT_EQ(target->getMovementRange(), movementBefore);
		EXPECT_EQ(target->getInitiative(), initiativeBefore);
		EXPECT_EQ(target->getAvailableHealth(), targetHealthBefore);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell->getId();
		action.aimToUnit(target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(targetHealthBefore - target->getAvailableHealth(), previewDamage);
		EXPECT_EQ(adjacent->getAvailableHealth(), adjacentHealthBefore);
		EXPECT_EQ(target->getMovementRange(), movementBefore - (legacySlow ? 2u : 0u));
		EXPECT_EQ(target->getInitiative(), initiativeBefore);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 5);
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

class NewHorizonsSlowRankTest : public NewHorizonsDirectDamageMechanicsTest,
	public ::testing::WithParamInterface<SlowRankCase>
{};

class NewHorizonsSlowLegacyProfileTest : public NewHorizonsDirectDamageMechanicsTest,
	public ::testing::WithParamInterface<SlowLegacyCase>
{};

class NewHorizonsControlledBlastDamageTest : public NewHorizonsDirectDamageMechanicsTest,
	public ::testing::WithParamInterface<ControlledBlastSpellCase>
{};

class NewHorizonsHavocDamagePerkTest : public NewHorizonsDirectDamageMechanicsTest,
	public ::testing::WithParamInterface<HavocDamagePerkCase>
{};

INSTANTIATE_TEST_SUITE_P(V3SchoolRank, NewHorizonsSlowRankTest,
	::testing::Values(
		SlowRankCase{0, 100, -30, "NoSchoolRank"},
		SlowRankCase{1, 115, -31, "Basic"},
		SlowRankCase{2, 130, -33, "Advanced"},
		SlowRankCase{3, 145, -34, "Expert"}),
	[](const ::testing::TestParamInfo<SlowRankCase> & info)
	{
		return std::string(info.param.name);
	});

INSTANTIATE_TEST_SUITE_P(SavedLegacyRules, NewHorizonsSlowLegacyProfileTest,
	::testing::Values(
		SlowLegacyCase{newHorizonsMagic::RULESET_VERSION, "V1"},
		SlowLegacyCase{newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION, "V2"}),
	[](const ::testing::TestParamInfo<SlowLegacyCase> & info)
	{
		return std::string(info.param.name);
	});

INSTANTIATE_TEST_SUITE_P(CenteredSpell, NewHorizonsControlledBlastDamageTest,
	::testing::Values(
		ControlledBlastSpellCase{"core:fireball", "core:pikeman", false, false, 25, "Fireball"},
		ControlledBlastSpellCase{"core:inferno", "core:pikeman", true, false, 70, "HypnotizedFriendlyInferno"},
		ControlledBlastSpellCase{"core:meteorShower", "core:archangel", false, true, 110, "DoubleWideMeteorShower"}),
	[](const ::testing::TestParamInfo<ControlledBlastSpellCase> & info)
	{
		return std::string(info.param.name);
	});

INSTANTIATE_TEST_SUITE_P(ActiveHavocPerk, NewHorizonsHavocDamagePerkTest,
	::testing::Values(
		HavocDamagePerkCase{"core:fireball", pyromancerPerkKey, 15, 25, 8, 0, "PyromancerFireballAtZero"},
		HavocDamagePerkCase{"core:fireball", pyromancerPerkKey, 15, 25, 8, 20, "PyromancerFireballWithPower"},
		HavocDamagePerkCase{"core:inferno", pyromancerPerkKey, 15, 70, 12, 0, "PyromancerInfernoAtZero"},
		HavocDamagePerkCase{"core:inferno", pyromancerPerkKey, 15, 70, 12, 20, "PyromancerInfernoWithPower"},
		HavocDamagePerkCase{"core:iceBolt", cryomancerPerkKey, 20, 45, 10, 0, "CryomancerIceBoltAtZero"},
		HavocDamagePerkCase{"core:iceBolt", cryomancerPerkKey, 20, 45, 10, 20, "CryomancerIceBoltWithPower"},
		HavocDamagePerkCase{"core:frostRing", cryomancerPerkKey, 20, 55, 11, 0, "CryomancerFrostRingAtZero"},
		HavocDamagePerkCase{"core:frostRing", cryomancerPerkKey, 20, 55, 11, 20, "CryomancerFrostRingWithPower"}),
	[](const ::testing::TestParamInfo<HavocDamagePerkCase> & info)
	{
		return std::string(info.param.name);
	});

TEST_P(NewHorizonsSlowRankTest, SavedV3MagnitudeDurationPreviewAndAuthoritativeCastAgree)
{
	const auto rank = GetParam();
	prepareSlow(newHorizonsMagic::CURRENT_RULESET_VERSION, rank.rank, 50);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);
	ASSERT_EQ(attackerSideHero->getEffectPower(spell), 50);
	const auto description = newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, rank.rank);
	EXPECT_NE(description.find("Fixed base duration: 2 rounds"), std::string::npos);
	EXPECT_NE(description.find("before target-specific specialties"), std::string::npos);
	EXPECT_NE(description.find(std::to_string(-rank.expectedMagnitude) + "%"), std::string::npos);

	const int32_t movementBefore = target->getMovementRange();
	const int32_t initiativeBefore = target->getInitiative();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto spellCost = attackerSideHero->getSpellCost(spell);
	const auto originalSlow = slowBonus(target);
	EXPECT_EQ(originalSlow, nullptr);

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	auto * projectedTarget = predicted.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	spells::Target projectedAim;
	projectedAim.emplace_back(projectedTarget);
	spells::BattleCast preview(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	auto previewMechanics = spell->battleMechanics(&preview);
	EXPECT_EQ(previewMechanics->getSchoolRankPowerCoefficientPercent(), rank.expectedCoefficientPercent);
	EXPECT_EQ(previewMechanics->getEffectDuration(), 2);
	const auto affected = previewMechanics->getAffectedStacks(projectedAim);
	ASSERT_EQ(affected.size(), 1u);
	EXPECT_EQ(affected.front()->unitId(), target->unitId());
	previewMechanics->castEval(predicted.getServerCallback(), projectedAim);
	projectedTarget = predicted.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedTarget, nullptr);

	const auto previewSlow = slowBonus(projectedTarget);
	ASSERT_NE(previewSlow, nullptr);
	EXPECT_EQ(previewSlow->type, BonusType::STACKS_INITIATIVE);
	EXPECT_EQ(previewSlow->valType, BonusValueType::ADDITIVE_VALUE);
	EXPECT_EQ(previewSlow->val, rank.expectedMagnitude);
	EXPECT_EQ(previewSlow->turnsRemain, 2);
	EXPECT_EQ(projectedTarget->getMovementRange(), movementBefore);
	EXPECT_EQ(projectedTarget->getInitiative(), initiativeBefore * (100 + rank.expectedMagnitude) / 100);
	EXPECT_EQ(target->getMovementRange(), movementBefore);
	EXPECT_EQ(target->getInitiative(), initiativeBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto authoritativeSlow = slowBonus(target);
	ASSERT_NE(authoritativeSlow, nullptr);
	EXPECT_EQ(authoritativeSlow->type, BonusType::STACKS_INITIATIVE);
	EXPECT_EQ(authoritativeSlow->valType, BonusValueType::ADDITIVE_VALUE);
	EXPECT_EQ(authoritativeSlow->val, rank.expectedMagnitude);
	EXPECT_EQ(authoritativeSlow->turnsRemain, 2);
	EXPECT_EQ(target->getMovementRange(), movementBefore);
	EXPECT_EQ(target->getInitiative(), initiativeBefore * (100 + rank.expectedMagnitude) / 100);
	EXPECT_EQ(target->getInitiative(), projectedTarget->getInitiative());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - spellCost);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, SavedV3SlowCapsAtFiftyPercentInItsAppliedInitiativeBonus)
{
	prepareSlow(newHorizonsMagic::CURRENT_RULESET_VERSION, MasteryLevel::EXPERT, 300);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);

	const auto initiativeBefore = target->getInitiative();
	const auto movementBefore = target->getMovementRange();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto applied = slowBonus(target);
	ASSERT_NE(applied, nullptr);
	EXPECT_EQ(applied->val, -50);
	EXPECT_EQ(applied->turnsRemain, 2);
	EXPECT_EQ(target->getInitiative(), initiativeBefore / 2);
	EXPECT_EQ(target->getMovementRange(), movementBefore);
}

TEST_P(NewHorizonsSlowLegacyProfileTest, SavedV1AndV2KeepConfiguredRankMagnitudeAndPowerDuration)
{
	const auto profile = GetParam();
	prepareSlow(profile.version, MasteryLevel::ADVANCED, 50);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);
	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, MasteryLevel::ADVANCED)
		.find("Current Sorcery rank:"), std::string::npos);

	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const int legacyDuration = attackerSideHero->getEnchantPower(spell);
	const auto previewMechanics = spell->battleMechanics(&preview);
	EXPECT_EQ(previewMechanics->getSchoolRankPowerCoefficientPercent(), 100);
	EXPECT_EQ(previewMechanics->getEffectDuration(), legacyDuration);
	const auto initiativeBefore = target->getInitiative();
	const auto movementBefore = target->getMovementRange();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto spellCost = attackerSideHero->getSpellCost(spell);
	const auto baseDuration = spell->battleMechanics(&preview)->getEffectDuration();
	EXPECT_EQ(baseDuration, legacyDuration);

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	auto * projectedTarget = predicted.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	spells::Target projectedAim;
	projectedAim.emplace_back(projectedTarget);
	spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	auto predictionMechanics = spell->battleMechanics(&prediction);
	EXPECT_EQ(predictionMechanics->getEffectDuration(), legacyDuration);
	predictionMechanics->castEval(predicted.getServerCallback(), projectedAim);
	projectedTarget = predicted.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	const auto previewSlow = slowBonus(projectedTarget);
	ASSERT_NE(previewSlow, nullptr);
	EXPECT_EQ(previewSlow->val, -50);
	EXPECT_EQ(previewSlow->turnsRemain, legacyDuration);
	EXPECT_EQ(projectedTarget->getMovementRange(), movementBefore);
	EXPECT_EQ(projectedTarget->getInitiative(), initiativeBefore / 2);
	EXPECT_EQ(target->getInitiative(), initiativeBefore);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto applied = slowBonus(target);
	ASSERT_NE(applied, nullptr);
	EXPECT_EQ(applied->val, -50);
	EXPECT_EQ(applied->turnsRemain, legacyDuration);
	EXPECT_EQ(target->getInitiative(), initiativeBefore / 2);
	EXPECT_EQ(target->getInitiative(), projectedTarget->getInitiative());
	EXPECT_EQ(target->getMovementRange(), movementBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - spellCost);
}

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
	const auto mana = attackerSideHero->getManaAvailable();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	prediction.castEval(predicted.getServerCallback(), destination);
	EXPECT_EQ(before - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(), 68);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), 68);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), 68) << "Rejected second hero action must not apply damage";
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, V3SchoolRanksScaleOnlySavedCoefficientAndMultiSchoolUsesHighestOnce)
{
	forceRealHeroScale = true;
	authoredRules = savedV3Formula();
	authoredRules["spells"][arrowKey]["schools"].Vector().push_back(JsonNode("new-horizons:havoc"));
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	const SecondarySkill havoc(SecondarySkill::decode("new-horizons:havocMagic"));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto formula = newHorizonsMagic::spellDirectDamage(battle()->getMagicRules(), arrowKey);
	ASSERT_TRUE(formula);
	EXPECT_EQ(formula->base, 20);
	EXPECT_EQ(formula->powerCoefficient, 20);

	const std::array<int64_t, 4> expected{220, 250, 280, 310};
	const std::array<int, 4> expectedPercent{100, 115, 130, 145};
	for(int rank = MasteryLevel::NONE; rank <= MasteryLevel::EXPERT; ++rank)
	{
		attackerSideHero->setSecSkillLevel(sorcery, rank, ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
		cast.setSpellLevel(0);
		cast.setEffectPower(100);
		EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(battle()->getMagicRules(), attackerSideHero, spell->getId()),
			expectedPercent[static_cast<size_t>(rank)]);
		EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), expected[static_cast<size_t>(rank)])
			<< "No-rank/Basic/Advanced/Expert must leave the base at 20";
		EXPECT_EQ(spell->calculateDamage(attackerSideHero), expected[static_cast<size_t>(rank)])
			<< "The spellbook damage estimate must use the same ranked coefficient";
	}
	EXPECT_EQ(newHorizonsMagic::directDamageValue(battle()->getMagicRules(), arrowKey, 100, 10, 145), 310);

	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(battle()->getMagicRules(), attackerSideHero, spell->getId()), 145);
	spells::BattleCast multiSchool(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
	multiSchool.setSpellLevel(0);
	multiSchool.setEffectPower(100);
	EXPECT_EQ(spell->battleMechanics(&multiSchool)->getEffectValue(), 310);
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(battle()->getMagicRules(), attackerSideHero, spell->getId()), 145)
		<< "Two Expert school memberships apply one factor, not two";
	EXPECT_NE(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, 0).find("Expert School: 145% Spell Power damage coefficient."),
		std::string::npos);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, SpellcraftComposesWithSchoolRanksInBasisPointsAndLeavesTheBaseFixed)
{
	forceRealHeroScale = true;
	authoredRules = savedV3Formula();
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	const auto formula = newHorizonsMagic::spellDirectDamage(battle()->getMagicRules(), arrowKey);
	ASSERT_TRUE(formula);
	EXPECT_EQ(formula->base, 20);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId()), 12650);
	EXPECT_EQ(formula->evaluateBasisPoints(0, 10, 12650), 20)
		<< "Basic School × Basic Spellcraft must not multiply the fixed base";

	spells::BattleCast basicCast(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
	basicCast.setSpellLevel(0);
	basicCast.setEffectPower(100);
	EXPECT_EQ(spell->battleMechanics(&basicCast)->getEffectValue(), 273);
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), 273)
		<< "Spellbook forecast and cast effect must use the same fractional coefficient";

	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId()), 13800);
	spells::BattleCast advancedCast(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
	advancedCast.setSpellLevel(0);
	advancedCast.setEffectPower(100);
	EXPECT_EQ(spell->battleMechanics(&advancedCast)->getEffectValue(), 296);
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), 296);

	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId()), 18850);
	spells::BattleCast expertCast(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
	expertCast.setSpellLevel(0);
	expertCast.setEffectPower(100);
	EXPECT_EQ(spell->battleMechanics(&expertCast)->getEffectValue(), 397);
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), 397);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, EmpowerSpellUsesFinalWisdomAdjustedCostThresholdOfTwelve)
{
	// Basic Wisdom turns listed cost 12 into 11 (not eligible) and listed cost
	// 13 into 12 (eligible). The two cases exercise the boundary through saved
	// perk selection, the cast mechanics, the CSpell estimate, AI preview, and
	// authoritative application.
	verifyEmpowerSpellCostThreshold(12, 11, 260, 0);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, EmpowerSpellAppliesAtWisdomAdjustedCostTwelve)
{
	verifyEmpowerSpellCostThreshold(13, 12, 320, 25);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, OldV3SnapshotWithoutSpellcraftFieldKeepsSchoolOnlyCoefficient)
{
	forceRealHeroScale = true;
	authoredRules = savedV3Formula();
	authoredRules.Struct().erase("spellcraftEfficiencyPercent");
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId()), 14500);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
	cast.setSpellLevel(0);
	cast.setEffectPower(100);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), 310);
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), 310);

	const auto * slow = SpellID(SpellID::SLOW).toSpell();
	ASSERT_NE(slow, nullptr);
	const int ordinaryDuration = attackerSideHero->getEnchantPower(slow);
	spells::BattleCast oldProfileDuration(battle(), attackerSideHero, spells::Mode::HERO, slow);
	EXPECT_EQ(slow->battleMechanics(&oldProfileDuration)->getEffectDuration(), ordinaryDuration)
		<< "An older v3 profile without Spellcraft coefficients must keep its original duration formula";
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, GenericDamageFormulaUsesTheSameRankCoefficientWithoutMovingItsBase)
{
	forceRealHeroScale = true;
	authoredRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	selectedSpellKey = "core:implosion";
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(spell->isDamage());
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(battle()->getMagicRules(), selectedSpellKey));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
	cast.setSpellLevel(0);
	cast.setEffectPower(100);
	const int divisor = attackerSideHero->getEffectPowerDivisor(spell);
	const int64_t expected = spell->getLevelPower(0)
		+ static_cast<int64_t>(spell->getBasePower()) * 100 * 145 / (static_cast<int64_t>(divisor) * 100);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), expected);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, CureScalesOnlyItsSpellPowerTermAndTooltipShowsRank)
{
	forceRealHeroScale = true;
	authoredRules = savedV3Formula();
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto * cure = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(cure, nullptr);
	const SecondarySkill light(SecondarySkill::decode("new-horizons:lightMagic"));
	const std::array<int64_t, 4> expected{175, 197, 220, 242};
	for(int rank = MasteryLevel::NONE; rank <= MasteryLevel::EXPERT; ++rank)
	{
		attackerSideHero->setSecSkillLevel(light, rank, ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::PASSIVE, cure);
		cast.setSpellLevel(0);
		cast.setEffectPower(100);
		EXPECT_EQ(cure->battleMechanics(&cast)->getEffectValue(), expected[static_cast<size_t>(rank)])
			<< "The fixed 25 HP remains unchanged";
	}
	const auto description = newHorizonsMagic::spellDescriptionForHero(attackerSideHero, cure, 0);
	EXPECT_NE(description.find("Base healing is 25 + 2.175 \u00d7 Spell Power HP"), std::string::npos);
	EXPECT_NE(description.find("Expert School: 145% Spell Power-derived healing."), std::string::npos);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, V2SavedDamageFormulaIgnoresInstalledSchoolRankFactors)
{
	forceRealHeroScale = true;
	authoredRules = savedFormula();
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), 2);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
	cast.setSpellLevel(0);
	cast.setEffectPower(100);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), 220);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, V1IceBoltRetainsLegacySlowInAuthoritativeCastAndAiPreview)
{
	authoredRules = savedV1MagicRules();
	verifyIceBoltSavedProfile(true, newHorizonsMagic::RULESET_VERSION);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, V2IceBoltRetainsLegacySlowInAuthoritativeCastAndAiPreview)
{
	authoredRules = savedFormula();
	verifyIceBoltSavedProfile(true, newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, V3IceBoltIsDamageOnlyInAuthoritativeCastAndAiPreview)
{
	authoredRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	verifyIceBoltSavedProfile(false, newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION, 65);
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
	const auto manaBefore = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), 170);
	EXPECT_EQ(adjacent->getAvailableHealth(), adjacentBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 5);
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
	const auto manaBefore = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(targetBefore - target->getAvailableHealth(), 41);
	EXPECT_EQ(adjacentBefore - adjacent->getAvailableHealth(), 41);
	EXPECT_EQ(distant->getAvailableHealth(), distantBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 5);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, FrostRingLeavesCenterSafeAndDamagesOnlyTheSurroundingRing)
{
	forceRealHeroScale = true;
	usePerks = true;
	fixtureActivePerkIds = {controlledBlastPerkKey};
	selectedSpellKey = "core:frostRing";
	authoredRules = savedV3Formula();
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	const auto havoc = SecondarySkill(SecondarySkill::decode(havocMagicKey));
	ASSERT_TRUE(havoc.hasValue());
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	selectControlledBlast();
	std::vector<CStack *> ring;
	for(const auto hex : BattleHexArray::getNeighbouringTiles(target->getPosition()))
		ring.push_back(addStack(ring.size() % 2 == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER,
			creatureByName("core:pikeman"), hex, 1000));
	ASSERT_EQ(ring.size(), 6u);
	auto * distant = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 2), 1000);
	const auto centerBefore = target->getAvailableHealth();
	const auto expectedDamage = spell->calculateDamage(attackerSideHero);
	ASSERT_GT(expectedDamage, 0);
	std::vector<int64_t> ringBefore;
	for(const auto * stack : ring)
		ringBefore.push_back(stack->getAvailableHealth());
	const auto distantBefore = distant->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToHex(target->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getAvailableHealth(), centerBefore);
	for(size_t index = 0; index < ring.size(); ++index)
		EXPECT_EQ(ringBefore[index] - ring[index]->getAvailableHealth(), expectedDamage) << "ring index " << index;
	EXPECT_EQ(distant->getAvailableHealth(), distantBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 8);
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
	const auto manaBefore = attackerSideHero->getManaAvailable();
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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 13);
}

TEST_P(NewHorizonsControlledBlastDamageTest, AcceptedCenteredCastSpareOnlyItsFriendlyCenter)
{
	const auto & testCase = GetParam();
	prepareControlledBlastSpell(testCase.spellId);
	const auto centerSide = testCase.centerIsHypnotized ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	auto * center = addStack(centerSide, creatureByName(testCase.centerCreatureId), BattleHex(rightHex), 1000);
	ASSERT_NE(center, nullptr);
	if(testCase.centerIsHypnotized)
		center->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(center), attackerSideHero->getOwner())
		<< "Controlled Blast follows current control rather than the stack's original side";
	if(testCase.centerIsDoubleWide)
		ASSERT_TRUE(center->doubleWide());
	else
		ASSERT_FALSE(center->doubleWide());

	BattleHex aimHex = center->getPosition();
	if(testCase.centerIsDoubleWide)
	{
		ASSERT_EQ(center->getHexes().size(), 2u);
		aimHex = center->getHexes()[1];
	}
	ASSERT_TRUE(center->getHexes().contains(aimHex));
	const auto formula = newHorizonsMagic::spellDirectDamage(battle()->getMagicRules(), spell->getJsonKey());
	ASSERT_TRUE(formula.has_value());
	EXPECT_EQ(formula->base, testCase.flatBase);
	const auto ordinaryDamage = spell->calculateDamage(attackerSideHero);
	EXPECT_EQ(ordinaryDamage, testCase.flatBase)
		<< "At zero Spell Power the school coefficient and Controlled Blast leave the authored base intact";

	BattleHex firstAdjacent = BattleHex::INVALID;
	BattleHex secondAdjacent = BattleHex::INVALID;
	for(const auto hex : BattleHexArray::getNeighbouringTiles(aimHex))
	{
		if(center->getHexes().contains(hex))
			continue;
		if(!firstAdjacent.isValid())
			firstAdjacent = hex;
		else
		{
			secondAdjacent = hex;
			break;
		}
	}
	ASSERT_TRUE(firstAdjacent.isValid());
	ASSERT_TRUE(secondAdjacent.isValid());
	auto * adjacentAlly = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), firstAdjacent, 1000);
	auto * adjacentEnemy = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), secondAdjacent, 1000);
	auto * distant = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(8, 0), 1000);
	ASSERT_NE(adjacentAlly, nullptr);
	ASSERT_NE(adjacentEnemy, nullptr);
	ASSERT_NE(distant, nullptr);
	ASSERT_TRUE(vstd::contains(BattleHexArray::getNeighbouringTiles(aimHex), firstAdjacent));
	ASSERT_TRUE(vstd::contains(BattleHexArray::getNeighbouringTiles(aimHex), secondAdjacent));

	// With the fixture's saved perk rule active but no selected perk, the same
	// accepted spell geometry still damages its friendly center.
	const auto unselectedPreview = previewAreaDamageAtHex(aimHex);
	ASSERT_TRUE(unselectedPreview.contains(center->unitId()));
	EXPECT_EQ(unselectedPreview.at(center->unitId()), testCase.flatBase);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(havocMagicKey, controlledBlastPerkKey));

	selectControlledBlast();
	const auto selectedPreview = previewAreaDamageAtHex(aimHex);
	ASSERT_TRUE(selectedPreview.contains(center->unitId()));
	ASSERT_TRUE(selectedPreview.contains(adjacentAlly->unitId()));
	ASSERT_TRUE(selectedPreview.contains(adjacentEnemy->unitId()));
	ASSERT_TRUE(selectedPreview.contains(distant->unitId()));
	EXPECT_EQ(selectedPreview.at(center->unitId()), 0);
	EXPECT_EQ(selectedPreview.at(adjacentAlly->unitId()), testCase.flatBase);
	EXPECT_EQ(selectedPreview.at(adjacentEnemy->unitId()), testCase.flatBase);
	EXPECT_EQ(selectedPreview.at(distant->unitId()), 0);
	if(testCase.centerIsDoubleWide)
	{
		const auto otherFootprintPreview = previewAreaDamageAtHex(center->getHexes()[0]);
		ASSERT_TRUE(otherFootprintPreview.contains(center->unitId()));
		EXPECT_EQ(otherFootprintPreview.at(center->unitId()), 0)
			<< "Either occupied footprint can be used as the controlled center";
	}

	if(std::string_view(testCase.spellId) == "core:fireball")
	{
		auto * isolatedCenter = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(10, 1), 1000);
		ASSERT_NE(isolatedCenter, nullptr);
		for(const auto neighbor : BattleHexArray::getNeighbouringTiles(isolatedCenter->getPosition()))
			EXPECT_EQ(battle()->battleGetUnitByPos(neighbor, false), nullptr)
				<< "isolated hover center must have no neighboring stacks";
		const auto hover = battle()->getSpellEffectValue(spell, attackerSideHero,
			spells::Mode::HERO, isolatedCenter->getPosition());
		ASSERT_NE(hover, nullptr);
		EXPECT_EQ(hover->hpDelta, 0)
			<< "An isolated protected center must have zero hover damage";
	}

	const auto centerBefore = center->getAvailableHealth();
	const auto allyBefore = adjacentAlly->getAvailableHealth();
	const auto enemyBefore = adjacentEnemy->getAvailableHealth();
	const auto distantBefore = distant->getAvailableHealth();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToHex(aimHex);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(centerBefore - center->getAvailableHealth(), 0);
	EXPECT_EQ(allyBefore - adjacentAlly->getAvailableHealth(), testCase.flatBase);
	EXPECT_EQ(enemyBefore - adjacentEnemy->getAvailableHealth(), testCase.flatBase);
	EXPECT_EQ(distant->getAvailableHealth(), distantBefore);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, ActiveControlledBlastDoesNotExemptArmageddon)
{
	forceRealHeroScale = true;
	usePerks = true;
	fixtureActivePerkIds = {controlledBlastPerkKey};
	selectedSpellKey = "core:armageddon";
	authoredRules = savedV3Formula();
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const auto havoc = SecondarySkill(SecondarySkill::decode(havocMagicKey));
	ASSERT_TRUE(havoc.hasValue());
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	selectControlledBlast();
	auto * friendly = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(8, 7), 1000);
	ASSERT_NE(friendly, nullptr);
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = target->getAvailableHealth();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.stackNumber = -1;
	action.aimToHex(BattleHex::INVALID);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(friendlyBefore - friendly->getAvailableHealth(), 150);
	EXPECT_EQ(enemyBefore - target->getAvailableHealth(), 150);
}

TEST_P(NewHorizonsHavocDamagePerkTest, ScalesOnlySpellPowerDamageAndMatchesAcceptedCast)
{
	const auto & testCase = GetParam();
	forceRealHeroScale = true;
	usePerks = true;
	fixtureActivePerkIds = {testCase.perkId};
	authoredRules = savedV3Formula();
	selectedSpellKey = testCase.spellId;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,
		testCase.rawSpellPower, ChangeValueMode::ABSOLUTE);
	const auto havoc = SecondarySkill(SecondarySkill::decode(havocMagicKey));
	ASSERT_TRUE(havoc.hasValue());
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(havocMagicKey, testCase.perkId));

	const auto formula = newHorizonsMagic::spellDirectDamage(battle()->getMagicRules(), spell->getJsonKey());
	ASSERT_TRUE(formula.has_value());
	ASSERT_EQ(formula->base, testCase.flatBase);
	ASSERT_EQ(formula->powerCoefficient, testCase.powerCoefficient);
	const int ordinaryCoefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId());
	const int ordinaryDamage = static_cast<int>(formula->evaluateBasisPoints(
		attackerSideHero->getEffectPower(spell), attackerSideHero->getEffectPowerDivisor(spell),
		ordinaryCoefficient));
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), ordinaryDamage)
		<< "Active saved rules do not apply Havoc bonuses before the perk is selected";

	attackerSideHero->applyPerkSelection({havocMagicKey, testCase.perkId});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(havocMagicKey, testCase.perkId));
	const int perkCoefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId(),
		testCase.additionalCoefficientPercent);
	EXPECT_EQ(perkCoefficient,
		ordinaryCoefficient * (100 + testCase.additionalCoefficientPercent) / 100);
	const int64_t expectedDamage = formula->evaluateBasisPoints(
		attackerSideHero->getEffectPower(spell), attackerSideHero->getEffectPowerDivisor(spell),
		perkCoefficient);
	if(testCase.rawSpellPower == 0)
		EXPECT_EQ(expectedDamage, testCase.flatBase)
			<< "The perk scales only the Spell Power-derived component";
	else
		EXPECT_GT(expectedDamage, ordinaryDamage)
			<< "The selected perk increases the existing school-scaled power component";
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), expectedDamage);

	CStack * damageTarget = target;
	BattleHex aimHex = target->getPosition();
	if(std::string_view(testCase.spellId) == "core:frostRing")
	{
		damageTarget = addStack(BattleSide::DEFENDER,
			creatureByName("core:pikeman"), BattleHex(rightHex + 1), 1000);
		ASSERT_NE(damageTarget, nullptr);
	}
	const auto before = damageTarget->getAvailableHealth();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), expectedDamage);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	if(std::string_view(testCase.spellId) == "core:iceBolt")
		action.aimToUnit(damageTarget);
	else
		action.aimToHex(aimHex);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - damageTarget->getAvailableHealth(), expectedDamage);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, MagicArrowOverchargeUsesTheSamePredictionAndAuthoritativeManaPath)
{
	forceRealHeroScale = true;
	authoredRules = savedV3Formula();
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	spells::Target destination;
	destination.emplace_back(target);
	spells::BattleCast legal(battle(), attackerSideHero, spells::Mode::HERO, spell);
	legal.setOvercharge(4);
	auto mechanics = spell->battleMechanics(&legal);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(destination, problem));
	EXPECT_EQ(mechanics->getEffectValue(), 496);

	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	prediction.setOvercharge(4);
	prediction.castEval(predicted.getServerCallback(), destination);
	EXPECT_EQ(before - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(), 496);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.spellOvercharge = 4;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), 496);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 8);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, MagicArrowOverchargeHealthChangeForecastCopiesWoundsAndTemporaryHealth)
{
	forceRealHeroScale = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);

	int64_t initialWound = 1;
	target->health.damage(initialWound);
	ASSERT_EQ(initialWound, 1);
	target->health.addTemporaryHitPoints(std::max<int64_t>(1, target->getMaxHealth() / 2));
	const auto healthBefore = target->getAvailableHealth();
	const auto countBefore = target->getCount();
	const auto firstHPBefore = target->getFirstHPleft();
	const auto temporaryHealthBefore = target->health.getTemporaryHitPoints();

	spells::Target aim;
	aim.emplace_back(target);
	spells::BattleCast legal(battle(), attackerSideHero, spells::Mode::HERO, spell);
	legal.setOvercharge(4);
	auto mechanics = spell->battleMechanics(&legal);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(aim, problem));
	ASSERT_EQ(mechanics->getEffectValue(), 352);

	// Compute expected casualties by applying the selected raw damage to a real
	// detached copy of the wounded, temporarily reinforced stack.
	auto simulatedState = target->acquireState();
	ASSERT_NE(simulatedState, nullptr);
	int64_t simulatedDamage = mechanics->getEffectValue();
	simulatedState->damage(simulatedDamage);
	ASSERT_EQ(simulatedDamage, mechanics->getEffectValue());
	const auto expectedHealthDelta = simulatedState->getAvailableHealth() - healthBefore;
	const auto expectedUnitsDelta = simulatedState->getCount() - countBefore;
	ASSERT_LT(expectedUnitsDelta, 0);

	const auto spellTarget = mechanics->canonicalizeTarget(aim);
	spells::effects::SpellEffectValue forecast;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		const auto affected = effect.transformTarget(mechanics.get(), aim, spellTarget);
		forecast += effect.getHealthChange(mechanics.get(), affected);
		return false;
	});
	EXPECT_EQ(forecast.hpDelta, expectedHealthDelta);
	EXPECT_EQ(forecast.hpDelta, -352);
	EXPECT_EQ(forecast.unitsDelta, expectedUnitsDelta);

	// The hover-style forecast mutates only its copy. The original stack still
	// has its wound, count, and temporary HP until the authoritative action.
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
	EXPECT_EQ(target->getCount(), countBefore);
	EXPECT_EQ(target->getFirstHPleft(), firstHPBefore);
	EXPECT_EQ(target->health.getTemporaryHitPoints(), temporaryHealthBefore);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.spellOvercharge = 4;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), -forecast.hpDelta);
	EXPECT_EQ(countBefore - target->getCount(), -forecast.unitsDelta);
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

	const auto mana = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.spellOvercharge = 4;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 7);
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
	const auto mana = attackerSideHero->getManaAvailable();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	prediction.setOvercharge(6);
	prediction.castEval(predicted.getServerCallback(), destination);
	EXPECT_EQ(before - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(), 656);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.spellOvercharge = 6;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), 656);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 10);
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
	// PerkState requires one selection in every earlier tier. Stormcaller is the
	// active Basic Havoc perk; its uniform Chain Lightning bonus does not change
	// the jump-retention ratios measured below.
	attackerSideHero->applyPerkSelection({
		"new-horizons:havocMagic", "new-horizons:havocMagic.stormcaller"});
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
		"new-horizons:havocMagic", "new-horizons:havocMagic.stormcaller"});
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
	// Build a valid Basic -> Advanced -> Expert selection history. Neither
	// predecessor changes Disintegrate's Annihilator reduction override.
	attackerSideHero->applyPerkSelection({
		"new-horizons:havocMagic", "new-horizons:havocMagic.stormcaller"});
	attackerSideHero->applyPerkSelection({
		"new-horizons:havocMagic", "new-horizons:havocMagic.conductor"});

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

TEST_F(NewHorizonsDirectDamageMechanicsTest, SpellPenetrationIgnoresTwentyPercentOfHostileTargetReductionAndMatchesPrediction)
{
	forceRealHeroScale = true;
	usePerks = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 24, ChangeValueMode::ABSOLUTE);

	const auto reduction = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::CREATURE_ABILITY, 50,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
	target->addNewBonus(reduction);

	spells::BattleCast withoutPerk(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&withoutPerk)->adjustEffectValue(target), 34);
	const auto spellCostBefore = attackerSideHero->getSpellCost(spell);

	selectSpellPenetration();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"),
		BattleHex(leftHex - 1), 1000);
	ASSERT_NE(friendly, nullptr);
	friendly->addNewBonus(std::make_shared<Bonus>(*reduction));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getEffectValue(), 68);
	EXPECT_EQ(mechanics->adjustEffectValue(target), 40);
	EXPECT_EQ(mechanics->adjustEffectValue(friendly), 34)
		<< "Spell Penetration applies only to hostile targets";
	ASSERT_EQ(attackerSideHero->getSpellCost(spell), spellCostBefore)
		<< "Spell Penetration must not alter the Mana cost";

	const auto targetHealthBefore = target->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageEnvironment environment(gameState(), nullptr);
	HypotheticBattle predicted(&environment, callback);
	const auto * projectedTarget = predicted.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	auto predictedMechanics = spell->battleMechanics(&prediction);
	EXPECT_EQ(predictedMechanics->adjustEffectValue(projectedTarget), 40);
	spells::Target predictedAim;
	predictedAim.emplace_back(projectedTarget);
	predictedMechanics->castEval(predicted.getServerCallback(), predictedAim);
	EXPECT_EQ(targetHealthBefore - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(), 40);
	EXPECT_EQ(target->getAvailableHealth(), targetHealthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(targetHealthBefore - target->getAvailableHealth(), 40);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - spellCostBefore);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, SpellPenetrationDoesNotBypassMagicResistanceOrSpellImmunity)
{
	forceRealHeroScale = true;
	usePerks = true;
	authoredRules = savedV3Formula();
	prepare();
	selectSpellPenetration();

	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));
	spells::BattleCast resistanceCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto resistanceMechanics = spell->battleMechanics(&resistanceCast);
	// Resistance remains a chance roll, not immunity, even at a raw100% bonus.
	// Spell Penetration affects damage protection, not the capped75% chance.
	EXPECT_EQ(target->magicResistance(), 75);
	EXPECT_TRUE(resistanceMechanics->isReceptive(target));
	EXPECT_TRUE(resistanceMechanics->canBeCastAt(spells::Target{spells::Destination(target)}));

	auto * immune = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
		BattleHex(rightHex - 1), 1000);
	ASSERT_NE(immune, nullptr);
	immune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
		BonusSubtypeID(spell->getId())));
	spells::BattleCast immunityCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto immunityMechanics = spell->battleMechanics(&immunityCast);
	EXPECT_FALSE(immunityMechanics->isReceptive(immune));
	EXPECT_FALSE(immunityMechanics->canBeCastAt(spells::Target{spells::Destination(immune)}));
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, TemporalistExtendsOnlyOrdinaryHeroSlowDuration)
{
	prepareSlow(newHorizonsMagic::CURRENT_RULESET_VERSION, MasteryLevel::BASIC, 50, true);
	const auto * slow = spell;
	const int v3SlowDuration = 2;

	spells::BattleCast ordinary(battle(), attackerSideHero, spells::Mode::HERO, slow);
	EXPECT_EQ(slow->battleMechanics(&ordinary)->getEffectDuration(), v3SlowDuration + 1);

	spells::BattleCast explicitDuration(battle(), attackerSideHero, spells::Mode::HERO, slow);
	explicitDuration.setEffectDuration(v3SlowDuration + 7);
	EXPECT_EQ(slow->battleMechanics(&explicitDuration)->getEffectDuration(), v3SlowDuration + 7);

	ControlledCaster nonHero(attackerSideHero);
	spells::BattleCast passive(battle(), &nonHero, spells::Mode::PASSIVE, slow);
	EXPECT_EQ(slow->battleMechanics(&passive)->getEffectDuration(), attackerSideHero->getEnchantPower(slow));

	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 0, ChangeValueMode::ABSOLUTE);
	spells::BattleCast rankLost(battle(), attackerSideHero, spells::Mode::HERO, slow);
	EXPECT_EQ(slow->battleMechanics(&rankLost)->getEffectDuration(), v3SlowDuration);
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 1, ChangeValueMode::ABSOLUTE);

	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	spells::BattleCast otherSpell(battle(), attackerSideHero, spells::Mode::HERO, haste);
	EXPECT_EQ(haste->battleMechanics(&otherSpell)->getEffectDuration(), attackerSideHero->getEnchantPower(haste));

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = slow->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto slowBonuses = target->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW))));
	ASSERT_FALSE(slowBonuses->empty());
	EXPECT_EQ(slowBonuses->front()->turnsRemain, v3SlowDuration + 1);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, PlannedTemporalistInAnOlderSnapshotStaysInactive)
{
	forceRealHeroScale = true;
	usePerks = true;
	prepare();
	const auto * slow = SpellID(SpellID::SLOW).toSpell();
	attackerSideHero->addSpellToSpellbook(slow->getId());
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 1, ChangeValueMode::ABSOLUTE);
	auto & saved = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	constexpr auto skillId = "new-horizons:sorceryMagic";
	constexpr auto perkId = "new-horizons:sorceryMagic.temporalist";
	bool foundTemporalist = false;
	for(auto & perk : saved.rules["skills"]["new-horizons:sorceryMagic"]["perks"].Vector())
	{
		if(perk["id"].String() == perkId)
		{
			perk["effect"]["status"].String() = "planned";
			foundTemporalist = true;
		}
	}
	ASSERT_TRUE(foundTemporalist);
	EXPECT_THROW(saved.select(skillId, perkId, 1), std::runtime_error);

	// Older saves can still contain a selection whose saved rules now mark it
	// planned. Restore that identity through the supported saved-state loader;
	// do not author it through the current selection API.
	auto oldSave = saved.toJson();
	JsonNode priorSelection;
	priorSelection["skillId"].String() = skillId;
	priorSelection["perkId"].String() = perkId;
	oldSave["selected"].Vector().push_back(std::move(priorSelection));
	saved = newHorizonsHeroes::PerkState::fromJson(oldSave);
	ASSERT_TRUE(saved.hasSelection(skillId, perkId));

	const int ordinaryDuration = attackerSideHero->getEnchantPower(slow);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, slow);
	EXPECT_EQ(slow->battleMechanics(&cast)->getEffectDuration(), ordinaryDuration);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = slow->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto slowBonuses = target->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW))));
	ASSERT_FALSE(slowBonuses->empty());
	EXPECT_EQ(slowBonuses->front()->turnsRemain, ordinaryDuration);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, MagicArrowRejectsOutOfRangeAndLegacyOverchargeAtomically)
{
	forceRealHeroScale = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();

	BattleAction tooMuch;
	tooMuch.actionType = EActionType::HERO_SPELL;
	tooMuch.side = BattleSide::ATTACKER;
	tooMuch.spell = spell->getId();
	tooMuch.spellOvercharge = 5; // SP 100 permits only four.
	tooMuch.aimToUnit(target);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), tooMuch));
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsDirectDamageMechanicsTest, MagicArrowRejectsMissingTargetBeforeSpendingManaOrAction)
{
	forceRealHeroScale = true;
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();

	BattleAction missingTarget;
	missingTarget.actionType = EActionType::HERO_SPELL;
	missingTarget.side = BattleSide::ATTACKER;
	missingTarget.spell = spell->getId();
	missingTarget.spellOvercharge = 4;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), missingTarget));
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);

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
	const auto legacyMana = attackerSideHero->getManaAvailable();
	BattleAction legacy;
	legacy.actionType = EActionType::HERO_SPELL;
	legacy.side = BattleSide::ATTACKER;
	legacy.spell = spell->getId();
	legacy.spellOvercharge = 1;
	legacy.aimToUnit(target);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), legacy));
	EXPECT_EQ(target->getAvailableHealth(), legacyHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), legacyMana);
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
	const auto mana = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - legacyListed);
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
