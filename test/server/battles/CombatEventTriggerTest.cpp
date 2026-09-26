/*
 * CombatEventTriggerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/combatScripts/ICombatEventScript.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/scripting/ScriptService.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../server/CGameHandler.h"

namespace
{

/// Marker the reaction to one event grants, so that the value accumulated on a unit says exactly
/// which of its events fired. Luck is used because nothing else in the scenario grants any.
constexpr int markerBeforeAttack = 1;
constexpr int markerAfterAttack = 2;
constexpr int markerBeforeAttacked = 4;
constexpr int markerAfterAttacked = 8;

}

/// The ON_COMBAT_EVENT bonus reacts to events of the unit carrying it with a predefined action -
/// granting a bonus or casting a spell. This pins that the four events of an attack reach it, on
/// both sides of that attack.
class CombatEventTriggerTest : public BattleTestFixture
{
public:
	static constexpr int32_t stackCount = 100;

	/// Makes `unit` grant itself a luck bonus of `marker` whenever `event` happens to it.
	static void reactWithMarker(CStack * unit, CombatEventType event, int marker)
	{
		BonusParametersOnCombatEvent::CombatEffectBonus effect;
		effect.bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::OTHER, marker, BonusSourceID());
		effect.targetEnemy = false;

		BonusParametersOnCombatEvent reaction;
		reaction.effects.emplace_back(effect);

		auto trigger = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ON_COMBAT_EVENT, BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype(static_cast<int>(event))));
		trigger->parameters = std::make_shared<BonusParameters>(reaction);

		unit->addNewBonus(trigger);
	}

	static int markersOf(const CStack * unit)
	{
		return unit->valOfBonuses(BonusType::LUCK);
	}
};

/// New Horizons-only script checks live in a separate fixture so the pre-existing core event test
/// continues to run unchanged when the content module is inactive.
class NewHorizonsFocusMagicTriggerTest : public CombatEventTriggerTest
{
public:
	static constexpr std::string_view focusMagicScriptName = "core:focusMagic";
	static constexpr std::string_view arcaneBreachScriptName = "core:arcaneBreach";
	static constexpr std::string_view arcaneBreachSpellName = "new-horizons:arcaneBreach";

	void runFocusMagic(CStack * shooter, CStack * other, const CombatEventPayload & payload,
		int32_t penetrationBasisPoints, BattleSide beneficiarySide) const
	{
		const auto & script = LIBRARY->scriptTypes()->getById(focusMagicScript);
		ASSERT_NE(script.combatEventScript, nullptr);

		JsonNode parameters;
		parameters["val"].Integer() = penetrationBasisPoints;
		parameters["beneficiarySide"].Integer() = static_cast<int32_t>(beneficiarySide);
		script.combatEventScript->run(gameHandler->spellcastEnvironment(), *battle(), CombatEventType::AFTER_ATTACK,
			shooter, other, parameters, payload);
	}

	std::vector<const Bonus *> arcaneMarks(const CStack * target) const
	{
		std::vector<const Bonus *> result;
		const auto subtype = BonusSubtypeID(arcaneBreachScript);
		for(const auto & bonus : target->getExportedBonusList())
		{
			if(bonus->type == BonusType::COMBAT_EVENT_TRIGGER
				&& bonus->subtype == subtype
				&& bonus->source == BonusSource::SPELL_EFFECT
				&& bonus->sid == BonusSourceID(arcaneBreachSpell))
				result.push_back(bonus.get());
		}
		return result;
	}

	void expectArcaneMarks(const CStack * target, std::vector<int32_t> expectedValues, BattleSide beneficiarySide) const
	{
		const auto marks = arcaneMarks(target);
		ASSERT_EQ(marks.size(), expectedValues.size());
		EXPECT_EQ(target->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER,
			BonusSubtypeID(arcaneBreachScript))->size(), expectedValues.size());

		std::vector<int32_t> actualValues;
		for(const auto * mark : marks)
		{
			EXPECT_EQ(mark->source, BonusSource::SPELL_EFFECT);
			EXPECT_EQ(mark->sid, BonusSourceID(arcaneBreachSpell));
			EXPECT_EQ(mark->duration, BonusDuration::N_TURNS);
			EXPECT_EQ(mark->turnsRemain, 2);
			ASSERT_NE(mark->parameters, nullptr);
			EXPECT_EQ(mark->parameters->toCustom<JsonNode>()["beneficiarySide"].Integer(),
				static_cast<int32_t>(beneficiarySide));
			actualValues.push_back(mark->val);
		}
		std::ranges::sort(actualValues);
		std::ranges::sort(expectedValues);
		EXPECT_EQ(actualValues, expectedValues);
	}

	void ageArcaneMarks(CStack * target) const
	{
		for(auto & bonus : target->getExportedBonusList())
		{
			if(bonus->type == BonusType::COMBAT_EVENT_TRIGGER
				&& bonus->subtype == BonusSubtypeID(arcaneBreachScript)
				&& bonus->source == BonusSource::SPELL_EFFECT
				&& bonus->sid == BonusSourceID(arcaneBreachSpell))
				bonus->turnsRemain = 1;
		}
	}

	void addArcaneMark(CStack * target, int32_t penetrationBasisPoints, BattleSide beneficiarySide) const
	{
		auto mark = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
			BonusSource::SPELL_EFFECT, penetrationBasisPoints, BonusSourceID(arcaneBreachSpell),
			BonusSubtypeID(arcaneBreachScript));
		mark->turnsRemain = 2;
		JsonNode parameters;
		parameters["beneficiarySide"].Integer() = static_cast<int32_t>(beneficiarySide);
		mark->parameters = std::make_shared<BonusParameters>(parameters);
		target->addNewBonus(mark);
	}

	void killStack(CStack * unit) const
	{
		auto state = unit->acquireState();
		auto damage = unit->getAvailableHealth();
		state->damage(damage);

		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		change.changedStacks.back().healthDelta = -damage;
		gameHandler->sendAndApply(change);
		ASSERT_FALSE(unit->alive());
	}

	ScriptID focusMagicScript;
	ScriptID arcaneBreachScript;
	SpellID arcaneBreachSpell;

protected:
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS));
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();

		const auto spell = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
			std::string(arcaneBreachSpellName));
		if(!spell)
			GTEST_SKIP() << "Requires the New Horizons Arcane Breach spell identity";

		const auto focusMagic = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
			std::string(focusMagicScriptName));
		ASSERT_TRUE(focusMagic.has_value());
		focusMagicScript = ScriptID(*focusMagic);

		const auto arcaneBreach = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
			std::string(arcaneBreachScriptName));
		ASSERT_TRUE(arcaneBreach.has_value());
		arcaneBreachScript = ScriptID(*arcaneBreach);
		arcaneBreachSpell = SpellID(*spell);
	}
};

TEST_F(CombatEventTriggerTest, everyAttackEventReachesItsUnit)
{
	startGame();
	startBattle();

	CStack * defender = addStack(BattleSide::ATTACKER, creatureByName("core:blackDragon"), BattleHex(leftHex), stackCount);
	CStack * attacker = addStack(BattleSide::DEFENDER, creatureByName("core:blackDragon"), BattleHex(rightHex), stackCount);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(attacker, nullptr);

	// a retaliation would fire the same events again, with the roles swapped
	blockRetaliation(attacker);

	reactWithMarker(attacker, CombatEventType::BEFORE_ATTACK, markerBeforeAttack);
	reactWithMarker(attacker, CombatEventType::AFTER_ATTACK, markerAfterAttack);
	reactWithMarker(defender, CombatEventType::BEFORE_ATTACKED, markerBeforeAttacked);
	reactWithMarker(defender, CombatEventType::AFTER_ATTACKED, markerAfterAttacked);

	ASSERT_EQ(markersOf(attacker), 0);
	ASSERT_EQ(markersOf(defender), 0);

	ASSERT_TRUE(attack(attacker, BattleHex(leftHex)));
	ASSERT_TRUE(defender->alive()) << "the victim has to survive to be checked";

	EXPECT_EQ(markersOf(attacker), markerBeforeAttack + markerAfterAttack);
	EXPECT_EQ(markersOf(defender), markerBeforeAttacked + markerAfterAttacked);
}

TEST_F(NewHorizonsFocusMagicTriggerTest, rangedStrikesAddAtMostThreeMarksAndRefreshWithoutChangingTheirPotencies)
{
	startGame();
	startBattle();

	CStack * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), stackCount);
	CStack * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), stackCount);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(target, nullptr);

	CombatEventPayload hit;
	hit.ranged = true;
	AttackedTarget attacked;
	attacked.unit = target;
	attacked.damage = 1;
	hit.targets.push_back(attacked);

	runFocusMagic(shooter, target, hit, 1000, BattleSide::ATTACKER);
	ASSERT_NO_FATAL_FAILURE(expectArcaneMarks(target, {1000}, BattleSide::ATTACKER));

	// Simulate marks nearing expiration so the script must refresh all existing entries on each hit.
	ageArcaneMarks(target);
	runFocusMagic(shooter, target, hit, 1500, BattleSide::ATTACKER);
	ASSERT_NO_FATAL_FAILURE(expectArcaneMarks(target, {1000, 1500}, BattleSide::ATTACKER));

	ageArcaneMarks(target);
	runFocusMagic(shooter, target, hit, 2000, BattleSide::ATTACKER);
	ASSERT_NO_FATAL_FAILURE(expectArcaneMarks(target, {1000, 1500, 2000}, BattleSide::ATTACKER));

	ageArcaneMarks(target);
	runFocusMagic(shooter, target, hit, 9000, BattleSide::ATTACKER);
	expectArcaneMarks(target, {1000, 1500, 2000}, BattleSide::ATTACKER);
	ASSERT_FALSE(server.battleLogLines.empty());
	EXPECT_THAT(server.battleLogLines.back(), testing::HasSubstr("refreshed at 3 of 3"));
	EXPECT_THAT(server.battleLogLines.back(), testing::HasSubstr("45.00%"));
	EXPECT_THAT(server.battleLogLines.back(), testing::Not(testing::HasSubstr("90.00%")));
}

TEST_F(NewHorizonsFocusMagicTriggerTest, onlyDamagingRangedHitsFromTheBeneficiarySideMarkLivingHostiles)
{
	startGame();
	startBattle();

	CStack * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), stackCount);
	CStack * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 2), stackCount);
	CStack * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), stackCount);
	CStack * deadEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 2), 1);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NE(deadEnemy, nullptr);
	ASSERT_NO_FATAL_FAILURE(killStack(deadEnemy));

	CombatEventPayload payload;
	payload.ranged = true;
	AttackedTarget target;
	target.damage = 1;

	target.unit = enemy;
	target.damage = 0;
	payload.targets = {target};
	runFocusMagic(shooter, enemy, payload, 1000, BattleSide::ATTACKER);

	target.unit = friendly;
	target.damage = 1;
	payload.targets = {target};
	runFocusMagic(shooter, friendly, payload, 1000, BattleSide::ATTACKER);

	target.unit = deadEnemy;
	payload.targets = {target};
	runFocusMagic(shooter, deadEnemy, payload, 1000, BattleSide::ATTACKER);

	target.unit = enemy;
	payload.targets = {target};
	runFocusMagic(shooter, enemy, payload, 1000, BattleSide::DEFENDER);

	payload.ranged = false;
	runFocusMagic(shooter, enemy, payload, 1000, BattleSide::ATTACKER);

	EXPECT_TRUE(arcaneMarks(enemy).empty());
	EXPECT_TRUE(arcaneMarks(friendly).empty());
	EXPECT_TRUE(arcaneMarks(deadEnemy).empty());
}

TEST_F(NewHorizonsFocusMagicTriggerTest, arcaneBreachMarkerDoesNotHandleCombatEvents)
{
	startGame();
	startBattle();

	const auto & script = LIBRARY->scriptTypes()->getById(arcaneBreachScript);
	ASSERT_NE(script.combatEventScript, nullptr);
	constexpr std::array events = {
		CombatEventType::BEFORE_ATTACK,
		CombatEventType::AFTER_ATTACK,
		CombatEventType::BEFORE_ATTACKED,
		CombatEventType::AFTER_ATTACKED,
		CombatEventType::WAIT,
		CombatEventType::DEFEND,
		CombatEventType::BEFORE_MOVE,
		CombatEventType::AFTER_MOVE,
		CombatEventType::UNIT_SPELLCAST,
		CombatEventType::BATTLE_START,
		CombatEventType::ROUND_START,
		CombatEventType::BATTLE_SETUP,
	};
	for(const auto event : events)
		EXPECT_FALSE(script.combatEventScript->handlesEvent(*battle(), event));
}

TEST_F(NewHorizonsFocusMagicTriggerTest, marksIgnoreOnlyTheirBeneficiarysRangedPhysicalCreatureDefense)
{
	startGame();
	startBattle();
	ASSERT_TRUE(newHorizonsMagic::rulesActive(battle()->getMagicRules()));

	CStack * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), stackCount);
	CStack * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), stackCount);
	CStack * wrongSideTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), stackCount);
	CStack * cappedTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 8), stackCount);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(wrongSideTarget, nullptr);
	ASSERT_NE(cappedTarget, nullptr);

	const BattleAttackInfo ranged(shooter, target, 0, true);
	const BattleAttackInfo melee(shooter, target, 0, false);
	BattleAttackInfo nonPhysicalShot(shooter, target, 0, true);
	nonPhysicalShot.physicalDamage = false;
	const auto firstShotBeforeMark = battle()->calculateDmgRange(ranged).damage;
	const auto meleeBeforeMark = battle()->calculateDmgRange(melee).damage;
	const auto nonPhysicalBeforeMark = battle()->calculateDmgRange(nonPhysicalShot).damage;
	const auto actualDefenseBeforeMark = target->getDefense(true);
	const auto healthBeforeMark = target->getAvailableHealth();
	ASSERT_EQ(actualDefenseBeforeMark, 20);
	EXPECT_EQ(firstShotBeforeMark.min, 4800);
	EXPECT_EQ(firstShotBeforeMark.max, 7200);

	CombatEventPayload damagingRangedHit;
	damagingRangedHit.ranged = true;
	AttackedTarget hit;
	hit.unit = target;
	hit.damage = 1;
	damagingRangedHit.targets.push_back(hit);

	// This event is the post-damage notification for the first shot. Only a later
	// damage calculation may benefit from the mark it adds.
	runFocusMagic(shooter, target, damagingRangedHit, 1000, BattleSide::ATTACKER);
	const auto oneMark = battle()->calculateDmgRange(ranged).damage;
	EXPECT_EQ(oneMark.min, 5200); // 10% of 20 Defense = 2 points ignored
	EXPECT_EQ(oneMark.max, 7800);

	runFocusMagic(shooter, target, damagingRangedHit, 1000, BattleSide::ATTACKER);
	runFocusMagic(shooter, target, damagingRangedHit, 1000, BattleSide::ATTACKER);
	const auto threeMarks = battle()->calculateDmgRange(ranged).damage;
	EXPECT_EQ(threeMarks.min, 6000); // 30% of 20 Defense = 6 points ignored
	EXPECT_EQ(threeMarks.max, 9000);

	EXPECT_EQ(battle()->calculateDmgRange(melee).damage.min, meleeBeforeMark.min);
	EXPECT_EQ(battle()->calculateDmgRange(melee).damage.max, meleeBeforeMark.max);
	EXPECT_EQ(battle()->calculateDmgRange(nonPhysicalShot).damage.min, nonPhysicalBeforeMark.min);
	EXPECT_EQ(battle()->calculateDmgRange(nonPhysicalShot).damage.max, nonPhysicalBeforeMark.max);
	EXPECT_EQ(target->getDefense(true), actualDefenseBeforeMark);
	EXPECT_EQ(target->getAvailableHealth(), healthBeforeMark);

	const BattleAttackInfo wrongSideShot(shooter, wrongSideTarget, 0, true);
	const auto wrongSideBaseline = battle()->calculateDmgRange(wrongSideShot).damage;
	addArcaneMark(wrongSideTarget, 2000, BattleSide::DEFENDER);
	const auto wrongSideResult = battle()->calculateDmgRange(wrongSideShot).damage;
	EXPECT_EQ(wrongSideResult.min, wrongSideBaseline.min);
	EXPECT_EQ(wrongSideResult.max, wrongSideBaseline.max);

	const BattleAttackInfo cappedShot(shooter, cappedTarget, 0, true);
	for(int mark = 0; mark < newHorizonsSorcery::ARCANE_BREACH_MAX_MARKS + 1; ++mark)
		addArcaneMark(cappedTarget, 10000, BattleSide::ATTACKER);
	const auto cappedResult = battle()->calculateDmgRange(cappedShot).damage;
	EXPECT_EQ(cappedResult.min, 7200); // four overvalued marks still cap at three x 2000 bps
	EXPECT_EQ(cappedResult.max, 10800);
}
