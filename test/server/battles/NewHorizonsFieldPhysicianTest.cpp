/*
 * NewHorizonsFieldPhysicianTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsCureHealing.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto machines = "new-horizons:warMachines";
class NewHorizonsFieldPhysicianTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons content";
	}
	bool captured = true;
	bool legacyMagic = false;
	CStack * patient = nullptr;
	const CStack * tent = nullptr;
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		rules.Struct().erase("additionalSpecialties");
		if(captured)
		{
			rules["additionalSpecialties"]["version"].Integer() = 1;
			rules["additionalSpecialties"]["fieldPhysician"]["healingPercentPerLevel"].Integer() = 5;
			rules["additionalSpecialties"]["fieldPhysician"]["healingMaximumPercent"].Integer() = 100;
		}
		// A legacy magic fixture cannot request modern starting-book migration.
		if(legacyMagic)
			rules["startingSkills"].Struct().erase("startingBookReplacements");
		ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(rules, true));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_EQ(magic["schoolSkills"].Struct().size(), 6u);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, legacyMagic ? JsonNode() : magic);
		JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
		capabilities["siege"]["outputs"]["firstAidHealing"]["base"].Integer() = 40;
		capabilities["siege"]["outputs"]["firstAidHealing"]["siegeCoefficientHalf"].Integer() = 0;
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, capabilities);
	}
	void rank(const char * skill, int value)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), value, ChangeValueMode::ABSOLUTE);
	}
	void prepare(const char * hero = "core:rion", int power = 10, bool healer = false, const char * advancedMachinePerk = nullptr)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(hero)));
		attackerSideHero->level = 20;
		if(newHorizonsMagic::rulesActive(attackerSideHero->getMagicRules()))
			for(const auto & [school, skill] : attackerSideHero->getMagicRules()["schoolSkills"].Struct())
				rank(skill.String().c_str(), MasteryLevel::NONE);
		rank("new-horizons:spellcraft", MasteryLevel::NONE);
		rank(machines, advancedMachinePerk ? MasteryLevel::ADVANCED : MasteryLevel::BASIC);
		attackerSideHero->applyPerkSelection({machines, "new-horizons:warMachines.surgeon"});
		if(advancedMachinePerk)
			attackerSideHero->applyPerkSelection({machines, advancedMachinePerk});
		if(healer)
		{
			rank("new-horizons:lightMagic", MasteryLevel::BASIC);
			attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.healer"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"));
		}
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		giveArtifact(attackerSideHero, ArtifactID::FIRST_AID_TENT, ArtifactPosition::MACH3);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:behemoth"), 10));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 100));
		startBattle();
		for(const auto * unit : battle()->battleGetStacksIf([](const CStack *) { return true; }))
		{
			if(unit->unitSide() == BattleSide::ATTACKER && unit->isFirstAidTent())
				tent = unit;
			if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == creatureByName("core:behemoth"))
				patient = const_cast<CStack *>(unit);
		}
		ASSERT_NE(patient, nullptr);
		ASSERT_NE(tent, nullptr);
		beginCombat();
	}
	void injure(CStack * target, int64_t amount)
	{
		auto state = target->acquireState();
		state->damage(amount, false);
		UnitChanges change(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -amount;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}
	void castAndExpect(int64_t expected)
	{
		ASSERT_NO_FATAL_FAILURE(injure(patient, 120));
		const auto health = patient->getAvailableHealth();
		const auto count = patient->getCount();
		spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::CURE).toSpell());
		EXPECT_EQ(preview.mechanicsForTarget({spells::Destination(patient)})->getEffectValue(), expected);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::CURE;
		action.aimToUnit(patient);
		const auto mana = attackerSideHero->getManaAvailable();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(patient->getAvailableHealth() - health, expected);
		EXPECT_EQ(patient->getCount(), count);
		EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	}
	void reachTent()
	{
		for(size_t i = 0; i < battle()->stacks.size() * 4; ++i)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			if(active->unitId() == tent->unitId())
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
		}
		FAIL() << "Tent did not reach its normal activation";
	}
};
}
TEST_F(NewHorizonsFieldPhysicianTest, PaidCureDoublesFixedHealingAtZeroPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:rion", 0));
	ASSERT_NO_FATAL_FAILURE(castAndExpect(50));
}
TEST_F(NewHorizonsFieldPhysicianTest, PaidCureDoublesBothComponentsWithoutResurrection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto description = attackerSideHero->getSpecialtyPresentation().description;
	EXPECT_NE(description.find("Cure and First Aid Tent"), std::string::npos);
	EXPECT_NE(description.find("includes Cure's fixed healing"), std::string::npos);
	ASSERT_NO_FATAL_FAILURE(castAndExpect(50 + 30));
}
TEST_F(NewHorizonsFieldPhysicianTest, PaidHealerCureKeepsScaledFixedAndPowerFloorsSeparate)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:rion", 10, true));
	ASSERT_NO_FATAL_FAILURE(castAndExpect(50 + 40)); // floor(15*1.15)=17; twice17, then floor(*1.20)=40.
}
TEST_F(NewHorizonsFieldPhysicianTest, GemHealerCureKeepsItsOldPowerOnlySpecialty)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gem", 10, true));
	ASSERT_NO_FATAL_FAILURE(castAndExpect(25 + 40));
}
TEST_F(NewHorizonsFieldPhysicianTest, HistoricalAbsentRionKeepsOrdinaryCureAndPolicyAbsenceThroughSave)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto heroID = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	ASSERT_NE(restored.getHero(heroID), nullptr);
	EXPECT_FALSE(newHorizonsHeroes::fieldPhysicianRules(restored.getHero(heroID)->getPrimaryGrowthRules()));
	EXPECT_EQ(newHorizonsMagic::cureHealingBudget(restored.getHero(heroID), 10, 10000), 40);
	ASSERT_NO_FATAL_FAILURE(castAndExpect(40));
}
TEST_F(NewHorizonsFieldPhysicianTest, TentMedicUsesEnhancedCreatureBudgetAndSurgeonRemovesOnlyOneAffliction)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:rion", 10, false, "new-horizons:warMachines.battlefieldMedic"));
	ASSERT_NO_FATAL_FAILURE(injure(patient, patient->getMaxHealth() + 3));
	ASSERT_NO_FATAL_FAILURE(reachTent());
	for(const auto kind : {"poison", "disease"})
	{
		const BonusSourceID source{BonusCustomSource(kind[0] == 'p' ? 1901 : 1902)};
		Bonus marker(BonusDuration::PERMANENT, BonusType::PHYSICAL_AFFLICTION, BonusSource::OTHER, 0, source);
		JsonNode parameters;
		parameters["kind"].String() = kind;
		parameters["applicationOrder"].Integer() = kind[0] == 'p' ? 1 : 2;
		marker.parameters = std::make_shared<BonusParameters>(parameters);
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(patient->unitId(), std::vector<Bonus>{marker});
		gameHandler->sendAndApply(effect);
	}
	ASSERT_EQ(physicalAfflictions::enumerate(*patient).size(), 2u);
	EXPECT_EQ(battle()->battleGetFirstAidHealingOutput(tent), 40);
	EXPECT_EQ(battle()->battleGetFirstAidCreatureHealingOutput(tent, patient), 80);
	EXPECT_EQ(battle()->battleGetBattlefieldMedicRestorationBudget(tent, patient), 40);
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, patient);
	EXPECT_EQ(preview.survivorHealedHP, 3);
	EXPECT_EQ(preview.restoredHP, 40);
	const auto health = patient->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeHeal(tent, patient)));
	EXPECT_EQ(patient->getAvailableHealth() - health, 43);
	const auto remaining = physicalAfflictions::enumerate(*patient);
	ASSERT_EQ(remaining.size(), 1u);
	EXPECT_EQ(remaining.front().kind, "disease");
}
TEST_F(NewHorizonsFieldPhysicianTest, TentWorkshopRepairRetainsOrdinaryOutput)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:rion", 10, false, "new-horizons:warMachines.fieldWorkshop"));
	auto * machine = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(4, 8), 1);
	ASSERT_NE(machine, nullptr);
	ASSERT_NO_FATAL_FAILURE(injure(machine, 100));
	ASSERT_NO_FATAL_FAILURE(reachTent());
	ASSERT_TRUE(battle()->battleCanRepairWarMachine(tent, machine));
	EXPECT_EQ(battle()->battleGetFirstAidCreatureHealingOutput(tent, machine), 40);
	const auto health = machine->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeHeal(tent, machine)));
	EXPECT_EQ(machine->getAvailableHealth() - health, 40);
}

TEST_F(NewHorizonsFieldPhysicianTest, GemMedicKeepsItsHistoricalUnenhancedCasualtyBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gem", 10, false, "new-horizons:warMachines.battlefieldMedic"));
	ASSERT_NO_FATAL_FAILURE(injure(patient, patient->getMaxHealth() + 3));
	ASSERT_NO_FATAL_FAILURE(reachTent());
	EXPECT_EQ(battle()->battleGetFirstAidHealingOutput(tent), 40);
	EXPECT_EQ(battle()->battleGetFirstAidCreatureHealingOutput(tent, patient), 80);
	EXPECT_EQ(battle()->battleGetBattlefieldMedicRestorationBudget(tent, patient), 20);
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, patient);
	EXPECT_EQ(preview.totalHealedHP(), 23);
	const auto health = patient->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeHeal(tent, patient)));
	EXPECT_EQ(patient->getAvailableHealth() - health, 23);
}

TEST_F(NewHorizonsFieldPhysicianTest, FirstLevelCureFloorsFixedAndPowerBonusesIndependently)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->level = 1;
	ASSERT_NO_FATAL_FAILURE(castAndExpect(26 + 15));
}
TEST_F(NewHorizonsFieldPhysicianTest, GuardianSpiritReceivesNoFieldPhysicianBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::decode("new-horizons:guardianSpirit"));
	spells::BattleCast rion(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto rionValue = rion.mechanicsForTarget({spells::Destination(patient)})->getEffectValue();
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:christian")));
	spells::BattleCast ordinary(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	EXPECT_EQ(ordinary.mechanicsForTarget({spells::Destination(patient)})->getEffectValue(), rionValue);
}

TEST_F(NewHorizonsFieldPhysicianTest, CapturedRionWithLegacyMagicKeepsOrdinaryCureAndDescribesTentOnly)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_TRUE(newHorizonsHeroes::fieldPhysicianRules(attackerSideHero->getPrimaryGrowthRules()));
	ASSERT_FALSE(newHorizonsMagic::cureEnabled(attackerSideHero->getMagicRules(), spell->getId()));
	const auto ordinary = spell->calculateRawEffectValue(attackerSideHero->getEffectLevel(spell), 10, 1,
		attackerSideHero->getEffectPowerDivisor(spell));
	ASSERT_GT(ordinary, 0);
	ASSERT_LT(ordinary, 120);
	const auto description = attackerSideHero->getSpecialtyPresentation().description;
	EXPECT_EQ(description.find("Cure and First Aid Tent"), std::string::npos);
	EXPECT_NE(description.find("First Aid Tent creature healing increases"), std::string::npos);
	EXPECT_NE(description.find("Cure retains its ordinary healing"), std::string::npos);
	EXPECT_NE(description.find("Current Tent healing increase: +100%."), std::string::npos);
	EXPECT_EQ(battle()->battleGetFirstAidCreatureHealingOutput(tent, patient), 80);
	ASSERT_NO_FATAL_FAILURE(castAndExpect(ordinary));
}
