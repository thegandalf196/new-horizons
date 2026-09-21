/*
 * NewHorizonsAreaFormulaTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"

// Future UNREGISTERED coverage of the existing area-damage primitive. This is
// core Fireball, NOT a substitute/acceptance claim for a new Sorcery spell.
class NewHorizonsAreaFormulaTest : public HeroCommandFixture
{
protected:
	bool useFormula = true;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(useFormula)
		{
			rules["rulesetVersion"].Integer() = 2;
			auto & formula = rules["spells"][SpellID(SpellID::FIREBALL).toSpell()->getJsonKey()]["directDamage"];
			formula["base"].Integer() = 20;
			formula["powerCoefficient"].Integer() = 20;
		}
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
	}

	void checkAreaCast()
	{
		prepareCommands(true);
		const SpellID id(SpellID::FIREBALL);
		const auto * spell = id.toSpell();
		attackerSideHero->addSpellToSpellbook(id);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 24, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->getEffectPower(spell), 24);
		ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);
		const auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("pikeman"), BattleHex(70), 100);
		const auto * first = addStack(BattleSide::DEFENDER, creatureByName("pikeman"), BattleHex(71), 100);
		const auto * second = addStack(BattleSide::DEFENDER, creatureByName("pikeman"), BattleHex(72), 100);
		const auto * distant = addStack(BattleSide::DEFENDER, creatureByName("pikeman"), BattleHex(120), 100);
		const auto expected = useFormula ? int64_t{68} : spell->calculateDamage(attackerSideHero);
		ASSERT_GT(expected, 0);
		if(!useFormula)
			ASSERT_NE(expected, 68) << "Legacy control must discriminate the saved formula";
		spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, spell);
		ASSERT_EQ(spell->battleMechanics(&probe)->getEffectValue(), expected);
		const auto friendlyHealth = friendly->getAvailableHealth();
		const auto firstHealth = first->getAvailableHealth();
		const auto secondHealth = second->getAvailableHealth();
		const auto distantHealth = distant->getAvailableHealth();
		ASSERT_LT(expected, friendlyHealth);
		ASSERT_LT(expected, firstHealth);
		ASSERT_LT(expected, secondHealth);
		const auto mana = attackerSideHero->mana;
		const auto cost = battle()->battleGetSpellCost(spell, attackerSideHero);
		const auto * active = battle()->battleActiveUnit();
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = id;
		action.aimToHex(BattleHex(71));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(friendly->getAvailableHealth(), friendlyHealth - expected);
		EXPECT_EQ(first->getAvailableHealth(), firstHealth - expected);
		EXPECT_EQ(second->getAvailableHealth(), secondHealth - expected);
		EXPECT_EQ(distant->getAvailableHealth(), distantHealth);
		EXPECT_EQ(attackerSideHero->mana, mana - cost);
		EXPECT_EQ(battle()->battleActiveUnit(), active);
		EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
		EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	}
};

TEST_F(NewHorizonsAreaFormulaTest, SavedFormulaAppliesToBothEnemiesAndFriendlyCollateral)
{
	checkAreaCast();
}

TEST_F(NewHorizonsAreaFormulaTest, LegacyAreaDamageRetainsRawCalculationAndCollateral)
{
	useFormula = false;
	checkAreaCast();
}
