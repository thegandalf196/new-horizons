/*
 * NewHorizonsCureTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/spells/CurePhysicalAffliction.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"

#include "BattleStartSnapshotFixture.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/CRandomGenerator.h"

namespace
{
constexpr auto cureKey = "core:cure";

// Extend historicalDescriptionRules' positive projection with v2 guild context
// required by the installed hero starting-book validation.
// Installed v3 clauses must not leak into a purported v2 saved roster.
JsonNode savedV2CureRules(const JsonNode & current)
{
	JsonNode result;
	for(const auto * field : {"schemaVersion", "schools", "schoolSkills", "skillReplacements", "adventureSpells",
		"mageGuildGeneration", "factions"})
		result[field] = current[field];
	result["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	result["adventureSpells"]["core:waterWalk"].Struct().erase("requireLegalDayEnd");
	for(const auto & [key, row] : current["spells"].Struct())
	{
		for(const auto * field : {"schools", "level", "costs", "ordinaryAcquisition", "directDamage", "active", "cureAfflictions"})
			if(row.Struct().contains(field))
				result["spells"][key][field] = row[field];
		if(row.Struct().contains("variant"))
			result["spells"][key]["active"].Bool() = false;
	}
	newHorizonsMagic::validateRules(result);
	EXPECT_EQ(result["mageGuildGeneration"], current["mageGuildGeneration"]);
	EXPECT_EQ(result["factions"], current["factions"]);
	EXPECT_TRUE(newHorizonsMagic::mageGuildGenerationActive(result));
	for(const auto * field : {"protectedAdventureBarriers", "heroLuck", "heroMorale", "creatureAbilities",
		"morale", "schoolRankPowerCoefficientPercent", "spellcraftEfficiencyPercent"})
		EXPECT_FALSE(result.Struct().contains(field)) << field;
	for(const auto & [key, row] : result["spells"].Struct())
	{
		for(const auto * field : {"selectedPlacement", "heroAccess", "variant", "earthquake", "structures",
			"restoration", "propagationLimit", "implosion", "ignoreInterveningBarriers", "temporaryMagicalEffectsOnly",
			"schoolRankDurations", "burnGroundedFlyers"})
			EXPECT_FALSE(row.Struct().contains(field)) << key << ':' << field;
		if(current["spells"][key].Struct().contains("variant"))
			EXPECT_FALSE(row["active"].Bool()) << key;
	}
	EXPECT_EQ(result["spells"][cureKey]["cureAfflictions"], current["spells"][cureKey]["cureAfflictions"]);
	return result;
}

SpellID naturePoisonSpell()
{
	static const SpellID poison(SpellID::decode(std::string(newHorizonsMagic::NATURE_POISON_SPELL)));
	return poison;
}

std::shared_ptr<Bonus> spellEffect(SpellID source, BonusType type, int value,
	BonusValueType valueType = BonusValueType::ADDITIVE_VALUE,
	BonusSubtypeID subtype = BonusSubtypeID())
{
	auto result = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
		value, BonusSourceID(source), subtype, valueType);
	result->turnsRemain = 3;
	return result;
}

class NewHorizonsCureTest : public HeroCommandFixture
{
protected:
	bool optIntoNewCure = true;
	bool enableHealerPerkRules = false;
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	bool disableFrozenRules = false;
	int freezingTouchChance = -1;
	bool deployOriginalCureActor = false;
	CStack * target = nullptr;
	CStack * poisonEnemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(enableHealerPerkRules)
		{
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		}
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(disableFrozenRules)
			rules.Struct().erase("creatureAbilities");
		else if(freezingTouchChance >= 0)
			rules["creatureAbilities"]["freezingTouchChancePercent"].Integer() = freezingTouchChance;
		if(magicVersion != newHorizonsMagic::CURRENT_RULESET_VERSION)
		{
			ASSERT_EQ(magicVersion, newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
			rules = savedV2CureRules(rules);
		}
		if(!optIntoNewCure)
			rules["spells"][cureKey].Struct().erase("cureAfflictions");
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		if(enableHealerPerkRules)
			captureLegacyPerkOfferPolicy(*map);
	}

	void prepare(int spellPower = 20, const std::string & creature = "core:pikeman", int count = 100,
		int poisonEnemyCount = 1)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		removeDeployedUnits();
		target = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), count);
		poisonEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), poisonEnemyCount);
		if(deployOriginalCureActor)
		{
			ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 1), nullptr);
		}
		beginCombat();
		ASSERT_NE(target, nullptr);
		ASSERT_NE(poisonEnemy, nullptr);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void injure(int64_t damage, CStack * stack = nullptr)
	{
		CStack * injuredStack = stack ? stack : target;
		auto state = injuredStack->acquireState();
		state->damage(damage);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(injuredStack->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		change.changedStacks.back().healthDelta = -damage;
		gameHandler->sendAndApply(change);
	}

	void addPoison(bool healthPenalty = false)
	{
		const auto poison = SpellID::POISON;
		target->addNewBonus(spellEffect(poison, BonusType::POISON, 30));
		if(healthPenalty)
			target->addNewBonus(spellEffect(poison, BonusType::STACK_HEALTH, -10,
				BonusValueType::PERCENT_TO_ALL));
	}

	void addFrozen(CStack * recipient = nullptr)
	{
		auto * frozenTarget = recipient ? recipient : target;
		SetStackEffect effects;
		effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(frozenTarget->unitId(), std::vector<Bonus>{
			newHorizonsFrozen::marker(BonusSourceID(creatureByName("core:iceElemental")),
				battle()->getBattle()->getRound())});
		gameHandler->sendAndApply(effects);
		ASSERT_TRUE(newHorizonsFrozen::isFrozen(*frozenTarget));
	}

	void addDisease()
	{
		const auto disease = SpellID::DISEASE;
		target->addNewBonus(spellEffect(disease, BonusType::PRIMARY_SKILL, -2,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::ATTACK)));
		target->addNewBonus(spellEffect(disease, BonusType::PRIMARY_SKILL, -2,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::DEFENSE)));
	}

	void addPhysicalPoison(int64_t baseDamage = 5, int32_t ticks = 3,
		CStack * stack = nullptr, int32_t sourceStackId = 17)
	{
		CStack * poisonedStack = stack ? stack : target;
		auto state = poisonedStack->acquireState();
		state->physicalPoisonBaseDamage = baseDamage;
		state->physicalPoisonActivationsRemaining = ticks;
		state->physicalPoisonSourceStackId = sourceStackId;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(poisonedStack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void addMagicalConditions()
	{
		target->addNewBonus(spellEffect(SpellID::CURSE, BonusType::PRIMARY_SKILL, -1,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::ATTACK)));
		target->addNewBonus(spellEffect(SpellID::SLOW, BonusType::STACKS_SPEED, -1));
		target->addNewBonus(spellEffect(SpellID::BERSERK, BonusType::PRIMARY_SKILL, -1,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::DEFENSE)));
	}

	bool hasSource(SpellID source) const
	{
		return !target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(source)))->empty();
	}

	JsonNode spellEffectsJson(SpellID source) const
	{
		JsonNode result;
		const auto bonuses = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(source)));
		for(const auto & bonus : *bonuses)
			result.Vector().push_back(bonus->toJsonNode());
		return result;
	}

	void preparePaidPuppetControl()
	{
		ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1, 100));
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		const SpellID puppet(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
		defenderSideHero->addSpellToSpellbook(puppet);
		defenderSideHero->addSpellToSpellbook(SpellID::CURE);
		const int chaosMagic = SecondarySkill::decode("new-horizons:chaosMagic");
		ASSERT_GE(chaosMagic, 0);
		defenderSideHero->setSecSkillLevel(SecondarySkill(chaosMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(defenderSideHero, 1000);
		const auto mana = defenderSideHero->getManaAvailable();
		const auto casts = battle()->battleCastSpells(BattleSide::DEFENDER);
		// Established paid-cast fixture path validates canBeCast/canBeCastAt and
		// executes BattleCast::cast, rather than fabricating a controller marker.
		ASSERT_TRUE(castOn(defenderSideHero, puppet, target));
		ASSERT_LT(defenderSideHero->getManaAvailable(), mana);
		ASSERT_EQ(battle()->battleCastSpells(BattleSide::DEFENDER), casts + 1);
		ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), target));
		ASSERT_EQ(target->unitSide(), BattleSide::ATTACKER);
		ASSERT_EQ(battle()->battleGetOwner(target), attackerSideHero->getOwner());
		ASSERT_EQ(battle()->battleGetActionController(target), defenderSideHero->getOwner());
	}

	BattleAction cureAction(const CStack * unit, SpellID affliction = SpellID::NONE) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::CURE;
		action.spellCureAffliction = affliction;
		action.aimToUnit(unit);
		return action;
	}

	BattleAction poisonAction(const CStack * unit) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = naturePoisonSpell();
		action.aimToUnit(unit);
		return action;
	}

	bool advanceUntilPhysicalPoisonActivation(CStack * stack, int32_t remainingBefore)
	{
		const size_t maximumActions = battle()->stacks.size() * 8 + 8;
		for(size_t actionIndex = 0; actionIndex < maximumActions; ++actionIndex)
		{
			if(stack->physicalPoisonActivationsRemaining < remainingBefore)
				return true;

			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			const BattleAction defend = BattleAction::makeDefend(active);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), defend))
				return false;
		}
		return stack->physicalPoisonActivationsRemaining < remainingBefore;
	}
};
}

TEST_F(NewHorizonsCureTest, OriginalAllyCureRemovesPhysicalPoisonButPreservesPaidPuppetControl)
{
	deployOriginalCureActor = true;
	ASSERT_NO_FATAL_FAILURE(preparePaidPuppetControl());
	ASSERT_NO_FATAL_FAILURE(addPhysicalPoison());
	addMagicalConditions();
	const auto control = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(control->size(), 1u);
	const auto marker = control->front()->toJsonNode();
	const auto markerOwner = control->front()->spellCasterOwner;
	const auto markerTurns = control->front()->turnsRemain;
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto casts = battle()->battleCastSpells(BattleSide::ATTACKER);
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const std::array<SpellID, 3> unrelated{SpellID::CURSE, SpellID::SLOW, SpellID::BERSERK};
	std::array<JsonNode, 3> unrelatedBefore;
	for(size_t index = 0; index < unrelated.size(); ++index)
		unrelatedBefore[index] = spellEffectsJson(unrelated[index]);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleGetActionController(battle()->battleActiveUnit()), attackerSideHero->getOwner());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), casts + 1);
	EXPECT_NE(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	const auto after = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(after->size(), 1u);
	EXPECT_EQ(after->front()->toJsonNode(), marker);
	EXPECT_EQ(after->front()->spellCasterOwner, markerOwner);
	EXPECT_EQ(after->front()->turnsRemain, markerTurns);
	EXPECT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), target));
	EXPECT_EQ(battle()->battleGetActionController(target), defenderSideHero->getOwner());
	EXPECT_EQ(battle()->battleGetOwner(target), attackerSideHero->getOwner());
	for(size_t index = 0; index < unrelated.size(); ++index)
	{
		EXPECT_TRUE(hasSource(unrelated[index]));
		EXPECT_EQ(spellEffectsJson(unrelated[index]), unrelatedBefore[index]);
	}
}

TEST_F(NewHorizonsCureTest, PuppetControllerCannotCureMechanicallyHostileTargetOrSpendItsFreshAction)
{
	ASSERT_NO_FATAL_FAILURE(preparePaidPuppetControl());
	advanceRound(); // Earn a new action; rejection must not merely mean Puppet spent it.
	ASSERT_NO_FATAL_FAILURE(addPhysicalPoison());
	// Peasants have only1HP, so injury would make casualties, not a healing
	// opportunity. A physical affliction provides a genuine friendly Cure target.
	ASSERT_NO_FATAL_FAILURE(addPhysicalPoison(5, 3, poisonEnemy));
	ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), target));
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleGetActionController(battle()->battleActiveUnit()), defenderSideHero->getOwner());
	spells::BattleCast cast(battle(), defenderSideHero, spells::Mode::HERO, SpellID(SpellID::CURE).toSpell());
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(SpellID(SpellID::CURE).toSpell()->battleMechanics(&cast)->canBeCast(problem));
	const auto state = target->acquireState()->save();
	const auto control = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(control->size(), 1u);
	const auto marker = control->front()->toJsonNode();
	const auto markerOwner = control->front()->spellCasterOwner;
	const auto mana = defenderSideHero->getManaAvailable();
	const auto casts = battle()->battleCastSpells(BattleSide::DEFENDER);
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
	auto action = cureAction(target, SpellID::POISON);
	action.side = BattleSide::DEFENDER;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), defenderSideHero->getOwner(), action));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::DEFENDER), casts);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), allowances);
	EXPECT_EQ(target->acquireState()->save(), state);
	const auto after = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(after->size(), 1u);
	EXPECT_EQ(after->front()->toJsonNode(), marker);
	EXPECT_EQ(after->front()->spellCasterOwner, markerOwner);
	EXPECT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), target));
	EXPECT_EQ(battle()->battleGetActionController(target), defenderSideHero->getOwner());
	EXPECT_EQ(battle()->battleGetOwner(target), attackerSideHero->getOwner());
}

TEST_F(NewHorizonsCureTest, OriginalAllyCureFrozenPreservesPaidPuppetControlAndSameRoundReceipt)
{
	deployOriginalCureActor = true;
	ASSERT_NO_FATAL_FAILURE(preparePaidPuppetControl());
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleGetActionController(battle()->battleActiveUnit()), attackerSideHero->getOwner());
	const auto control = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(control->size(), 1u);
	const auto marker = control->front()->toJsonNode();
	const auto markerOwner = control->front()->spellCasterOwner;
	const auto markerTurns = control->front()->turnsRemain;
	const auto health = target->getAvailableHealth();
	const auto round = target->frozenLastAppliedRound();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto casts = battle()->battleCastSpells(BattleSide::ATTACKER);
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	ASSERT_EQ(action.spellCureAffliction, SpellID::NONE);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(target->frozenLastAppliedRound(), round);
	EXPECT_FALSE(newHorizonsFrozen::canApply(*target, round));
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), casts + 1);
	EXPECT_NE(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	const auto after = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(after->size(), 1u);
	EXPECT_EQ(after->front()->toJsonNode(), marker);
	EXPECT_EQ(after->front()->spellCasterOwner, markerOwner);
	EXPECT_EQ(after->front()->turnsRemain, markerTurns);
	EXPECT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), target));
	EXPECT_EQ(battle()->battleGetActionController(target), defenderSideHero->getOwner());
	EXPECT_EQ(battle()->battleGetOwner(target), attackerSideHero->getOwner());
	EXPECT_EQ(target->unitSide(), BattleSide::ATTACKER);
	// Same authoritative admission used by the standalone Frozen regression.
	EXPECT_THROW(battle()->addUnitBonus(target->unitId(), {
		newHorizonsFrozen::marker(BonusSourceID(creatureByName("core:iceElemental")), round)}), std::invalid_argument);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), round);
}

TEST_F(NewHorizonsCureTest, PaidPuppetActiveAndCureCleansedFrozenReceiptsSurviveNativeStackCodecAndRebind)
{
	deployOriginalCureActor = true;
	ASSERT_NO_FATAL_FAILURE(preparePaidPuppetControl());
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	const auto round = target->frozenLastAppliedRound();
	const auto health = target->getAvailableHealth();
	ASSERT_EQ(health, target->getTotalHealth());
	const auto control = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(control->size(), 1u);
	const auto marker = control->front()->toJsonNode();
	const auto markerOwner = control->front()->spellCasterOwner;
	const auto markerTurns = control->front()->turnsRemain;
	const auto roundTrip = [&](bool frozen)
	{
		// Limited descriptor input: carry the actually produced exported bonuses
		// and validated Frozen receipt, not a fabricated control/status marker.
		// Native CStack wire does not encode full health/action state. This full-HP
		// constructor case is NOT a live army/world save or activation-resume test.
		CStackBasicDescriptor descriptor(target->creatureId(), target->unitBaseAmount());
		CStack native(&descriptor, target->unitOwner(), static_cast<int>(target->unitId()),
			target->unitSide(), target->unitSlot());
		native.initialPosition = target->getPosition();
		native.restoreFrozenApplicationRound(target->frozenLastAppliedRound());
		for(const auto & bonus : target->getExportedBonusList())
			native.addNewBonus(std::make_shared<Bonus>(*bonus));
		CMemorySerializer wire;
		ASSERT_NO_THROW(native.serialize(wire.oser));
		CStack decoded;
		ASSERT_NO_THROW(decoded.serialize(wire.iser));
		ASSERT_NO_THROW(decoded.localInit(battle()));
		EXPECT_EQ(decoded.unitSide(), target->unitSide());
		EXPECT_EQ(battle()->battleGetOwner(&decoded), attackerSideHero->getOwner());
		EXPECT_EQ(battle()->battleGetActionController(&decoded), defenderSideHero->getOwner());
		EXPECT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), &decoded));
		EXPECT_EQ(decoded.isNewHorizonsFrozen(), frozen);
		EXPECT_EQ(decoded.frozenLastAppliedRound(), round);
		EXPECT_FALSE(newHorizonsFrozen::canApply(decoded, round));
		EXPECT_EQ(decoded.getCount(), target->getCount());
		EXPECT_EQ(decoded.getAvailableHealth(), health);
		const auto restoredControl = decoded.getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
		ASSERT_EQ(restoredControl->size(), 1u);
		EXPECT_EQ(restoredControl->front()->toJsonNode(), marker);
		EXPECT_EQ(restoredControl->front()->spellCasterOwner, markerOwner);
		EXPECT_EQ(restoredControl->front()->turnsRemain, markerTurns);
		decoded.detachFromAll();
	};
	ASSERT_NO_FATAL_FAILURE(roundTrip(true));
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleGetActionController(battle()->battleActiveUnit()), attackerSideHero->getOwner());
	const auto mana = attackerSideHero->getManaAvailable();
	const auto casts = battle()->battleCastSpells(BattleSide::ATTACKER);
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action));
	ASSERT_FALSE(newHorizonsFrozen::isFrozen(*target));
	ASSERT_LT(attackerSideHero->getManaAvailable(), mana);
	ASSERT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), casts + 1);
	ASSERT_EQ(target->frozenLastAppliedRound(), round);
	ASSERT_NO_FATAL_FAILURE(roundTrip(false));
}

TEST_F(NewHorizonsCureTest, PaidPuppetFrozenCurePacketsReconstructDamagedBattleAndValidateNextActivation)
{
	deployOriginalCureActor = true;
	ASSERT_NO_FATAL_FAILURE(preparePaidPuppetControl());
	ASSERT_NO_FATAL_FAILURE(injure(3));
	ASSERT_NO_FATAL_FAILURE(injure(7, poisonEnemy));
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	const auto frozenRound = target->frozenLastAppliedRound();
	const auto damagedTargetHealth = target->getAvailableHealth();
	const auto manaBeforeCure = attackerSideHero->getManaAvailable();
	const auto castsBeforeCure = battle()->battleCastSpells(BattleSide::ATTACKER);
	auto cure = cureAction(target);
	cure.spellCurePhysicalAffliction = "frozen";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), cure));
	ASSERT_GT(target->getAvailableHealth(), damagedTargetHealth);
	ASSERT_FALSE(target->isNewHorizonsFrozen());
	ASSERT_LT(attackerSideHero->getManaAvailable(), manaBeforeCure);
	ASSERT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), castsBeforeCure + 1);
	ASSERT_NE(battle()->battleGetOwner(target), battle()->battleGetActionController(target));

	const auto targetId = target->unitId();
	const auto enemyId = poisonEnemy->unitId();
	const auto targetHealth = target->getAvailableHealth();
	const auto enemyHealth = poisonEnemy->getAvailableHealth();
	ASSERT_LT(enemyHealth, poisonEnemy->getTotalHealth());
	const auto originalOwner = battle()->battleGetOwner(target);
	const auto controller = battle()->battleGetActionController(target);
	const auto control = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(control->size(), 1u);
	const auto controlJson = control->front()->toJsonNode();
	const auto controlCaster = control->front()->spellCasterOwner;
	const auto round = battle()->getRound();
	const auto attackerAllowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto defenderAllowances = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
	const auto attackerCasts = battle()->battleCastSpells(BattleSide::ATTACKER);
	const auto defenderCasts = battle()->battleCastSpells(BattleSide::DEFENDER);
	const auto initialAttackerMana = battle()->getSide(BattleSide::ATTACKER).initialNormalSpellPoints;
	const auto initialDefenderMana = battle()->getSide(BattleSide::DEFENDER).initialNormalSpellPoints;
	const auto initialAttackerBuffer = battle()->getSide(BattleSide::ATTACKER).initialBufferSpellPoints;
	const auto initialDefenderBuffer = battle()->getSide(BattleSide::DEFENDER).initialBufferSpellPoints;
	const auto restoredId = gameState()->nextBattleID;

	// Composed packet reconstruction, NOT an emitted resume protocol or a world
	// save implementation. The established builder detaches real bonus descriptors;
	// CStack's wire omits full health/action state, carried by UnitChanges below.
	BattleStart outgoing;
	outgoing.battleID = restoredId;
	outgoing.info = battleStartFixture::snapshot(*battle(), gameState().get());
	for(const auto * unit : battle()->getStacksIf([](const CStack *) { return true; }))
		outgoing.info->getStack(unit->unitId(), false)->restoreFrozenApplicationRound(unit->frozenLastAppliedRound());
	BattleUnitsChanged outgoingUnits;
	outgoingUnits.battleID = restoredId;
	for(const auto * unit : battle()->getStacksIf([](const CStack *) { return true; }))
	{
		outgoingUnits.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
		outgoingUnits.changedStacks.back().data = unit->acquireState()->save();
	}
	CMemorySerializer wire;
	wire.oser & outgoing & outgoingUnits;
	wire.iser.cb = gameState().get();
	BattleStart incoming;
	BattleUnitsChanged incomingUnits;
	wire.iser & incoming & incomingUnits;
	ASSERT_NE(incoming.info, nullptr);
	EXPECT_EQ(incoming.info->getHeroActionAllowances(BattleSide::ATTACKER), attackerAllowances);
	EXPECT_EQ(incoming.info->getHeroActionAllowances(BattleSide::DEFENDER), defenderAllowances);

	// Tear down through the real visitor before attaching the replacement graph.
	// No saved live unit pointer is dereferenced after this boundary.
	BattleCancelled cancelled;
	cancelled.battleID = BattleID(0);
	gameHandler->sendAndApply(cancelled);
	target = nullptr;
	poisonEnemy = nullptr;
	ASSERT_TRUE(gameState()->currentBattles.empty());
	ASSERT_EQ(attackerSideHero->battle, nullptr);
	ASSERT_EQ(defenderSideHero->battle, nullptr);
	gameHandler->sendAndApply(incoming);
	gameHandler->sendAndApply(incomingUnits);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	ASSERT_EQ(battle()->getBattleID(), restoredId);
	target = battle()->getStack(targetId, false);
	poisonEnemy = battle()->getStack(enemyId, false);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(poisonEnemy, nullptr);
	EXPECT_EQ(target->getAvailableHealth(), targetHealth);
	EXPECT_EQ(poisonEnemy->getAvailableHealth(), enemyHealth);
	EXPECT_EQ(target->frozenLastAppliedRound(), frozenRound);
	EXPECT_FALSE(target->isNewHorizonsFrozen());
	EXPECT_FALSE(newHorizonsFrozen::canApply(*target, frozenRound));
	EXPECT_EQ(battle()->battleGetOwner(target), originalOwner);
	EXPECT_EQ(battle()->battleGetActionController(target), controller);
	const auto restoredControl = target->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	ASSERT_EQ(restoredControl->size(), 1u);
	EXPECT_EQ(restoredControl->front()->toJsonNode(), controlJson);
	EXPECT_EQ(restoredControl->front()->spellCasterOwner, controlCaster);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), attackerAllowances);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), defenderAllowances);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), attackerCasts);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::DEFENDER), defenderCasts);
	// The bootstrap packet intentionally resets INITIAL pools. Paid cast counts
	// and allowances survived, but paid mana pools did not: this is not faithful
	// whole-battle resume or world-save coverage. Do not bypass SetMana's guard.
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), initialAttackerMana);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), initialAttackerBuffer);
	EXPECT_EQ(defenderSideHero->getNormalSpellPoints(), initialDefenderMana);
	EXPECT_EQ(defenderSideHero->getBufferSpellPoints(), initialDefenderBuffer);

	// Advance only accepted ordinary actions, never force activeStack. Reach the
	// controlled unit naturally, reject its original owner's request, then accept
	// exactly the current controller's action through authoritative validation.
	for(unsigned int steps = 0; steps < 16; ++steps)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		if(active->unitId() == targetId)
			break;
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(restoredId,
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), targetId);
	ASSERT_EQ(battle()->getRound(), round);
	ASSERT_EQ(battle()->battleGetActionController(target), controller);
	const auto defend = BattleAction::makeDefend(target);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(restoredId, originalOwner, defend));
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), targetId);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(restoredId, controller, defend));
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceCleansesFullHealthAndPreservesRecipientStampAndInnateBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsFrozen::enabled(battle()->getMagicRules()));
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	const auto healthBefore = target->getAvailableHealth();
	const auto roundBefore = target->frozenLastAppliedRound();
	const auto ice = creatureByName("core:iceElemental");
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(ice)));
	const int manaBefore = attackerSideHero->getManaAvailable();
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
	EXPECT_EQ(target->frozenLastAppliedRound(), roundBefore);
	EXPECT_FALSE(newHorizonsFrozen::canApply(*target, roundBefore));
	EXPECT_TRUE(target->hasBonus(Selector::type()(BonusType::STACKS_SPEED)
		.And(Selector::source(BonusSource::CREATURE_ABILITY, BonusSourceID(ice)))));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 4);
	// Exercise the receiver, not only the read-only eligibility predicate: Cure
	// must not permit another same-round receipt to re-freeze the recipient.
	EXPECT_THROW(battle()->addUnitBonus(target->unitId(), {
		newHorizonsFrozen::marker(BonusSourceID(ice), roundBefore)}), std::invalid_argument);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), roundBefore);
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceLeavesPoisonAndDiseaseUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addPoison();
	addDisease();
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_TRUE(hasSource(SpellID::POISON));
	EXPECT_TRUE(hasSource(SpellID::DISEASE));
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceRejectsMissingUnknownAndConflictingSelectionWithoutSpending)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	action.spellCurePhysicalAffliction = "stoneGaze";
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	action.spellCurePhysicalAffliction = "frozen";
	action.spellCureAffliction = SpellID::POISON;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceRequiresCapturedCreatureRulesButNotPositiveProcChance)
{
	freezingTouchChance = 0;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsFrozen::chancePercent(battle()->getMagicRules()), 0);
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceRejectsAbsentSavedRulesWithoutSpending)
{
	disableFrozenRules = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
}

TEST_F(NewHorizonsCureTest, FrozenSurvivesOrdinarySorceryDispel)
{
	deployOriginalCureActor = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * actor = battle()->battleActiveUnit();
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(actor->unitId(), target->unitId());
	ASSERT_EQ(battle()->battleGetActionController(actor), attackerSideHero->tempOwner);
	ASSERT_TRUE(actor->canMove());
	ASSERT_FALSE(newHorizonsFrozen::isFrozen(*actor));
	const auto actorId = actor->unitId();
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	addMagicalConditions();
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	const auto stamp = target->frozenLastAppliedRound();
	auto action = cureAction(target);
	action.spell = SpellID::DISPEL;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), stamp);
	EXPECT_FALSE(hasSource(SpellID::SLOW));
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), actorId);
}

TEST_F(NewHorizonsCureTest, FrozenDirectMagicArrowBreaksWithoutShatterBonusAndPreservesStamp)
{
	ASSERT_NO_FATAL_FAILURE(prepare(20, "core:pikeman", 100, 100));
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	ASSERT_NO_FATAL_FAILURE(addFrozen(poisonEnemy));
	const auto roundBefore = poisonEnemy->frozenLastAppliedRound();
	const auto healthBefore = poisonEnemy->getAvailableHealth();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	const auto ordinaryDamage = mechanics->adjustEffectValue(poisonEnemy);
	ASSERT_GT(ordinaryDamage, 0);
	auto action = cureAction(poisonEnemy);
	action.spell = SpellID::MAGIC_ARROW;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_EQ(healthBefore - poisonEnemy->getAvailableHealth(), ordinaryDamage);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*poisonEnemy));
	EXPECT_EQ(poisonEnemy->frozenLastAppliedRound(), roundBefore);
	EXPECT_FALSE(newHorizonsFrozen::canApply(*poisonEnemy, roundBefore));
}

TEST_F(NewHorizonsCureTest, ZeroDamageMagicArrowStillThawsWithoutShatter)
{
	ASSERT_NO_FATAL_FAILURE(prepare(20, "core:pikeman", 100, 100));
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	// Canonical independent MDR caps at 95%, even for a 100% source. Use
	// the established final per-creature cap to produce a receptive zero hit.
	poisonEnemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::DAMAGE_RECEIVED_CAP, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(poisonEnemy->getMaxHealth() / 100, 0);
	ASSERT_NO_FATAL_FAILURE(addFrozen(poisonEnemy));
	const auto healthBefore = poisonEnemy->getAvailableHealth();
	const auto stamp = poisonEnemy->frozenLastAppliedRound();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	ASSERT_EQ(spell->battleMechanics(&parameters)->adjustEffectValue(poisonEnemy), 0);
	auto action = cureAction(poisonEnemy);
	action.spell = SpellID::MAGIC_ARROW;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_EQ(poisonEnemy->getAvailableHealth(), healthBefore);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*poisonEnemy));
	EXPECT_EQ(poisonEnemy->frozenLastAppliedRound(), stamp);
	ASSERT_EQ(server.castsOf(SpellID(SpellID::MAGIC_ARROW)).size(), 1u);
	for(const auto & injury : server.injuries)
		for(const auto & hit : injury.stacks)
			EXPECT_FALSE(hit.shattered());
}

TEST_F(NewHorizonsCureTest, HandOfFatePositiveImmediateSpillThawsWithoutShatterOrReroll)
{
	// Separate games are required: each paid spell consumes its hero action.
	// The companion zero-damage case below exercises the same sole candidate.
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1000, 1000));
	const SpellID hand(SpellID::decode("new-horizons:handOfFate"));
	ASSERT_TRUE(hand.hasValue());
	attackerSideHero->addSpellToSpellbook(hand);
	ASSERT_NO_FATAL_FAILURE(addFrozen(target));
	const auto stamp = target->frozenLastAppliedRound();
	const auto friendlyHP = target->getAvailableHealth();
	const auto enemyHP = poisonEnemy->getAvailableHealth();
	CRandomGenerator expected(seed);
	for(size_t recipient = 0; recipient < battle()->battleGetAllUnits(false).size(); ++recipient)
		expected.nextInt(0, 99);
	expected.nextInt(1, 1);
	gameHandler->randomizer->setSeed(seed);
	auto action = cureAction(poisonEnemy);
	action.spell = hand;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	ASSERT_GT(enemyHP - poisonEnemy->getAvailableHealth(), 0);
	EXPECT_EQ(friendlyHP - target->getAvailableHealth(), (enemyHP - poisonEnemy->getAvailableHealth()) / 2);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), stamp);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expected.nextInt());
	ASSERT_EQ(server.castsOf(hand).size(), 1u);
	ASSERT_FALSE(server.injuries.empty());
	for(const auto & injury : server.injuries)
		for(const auto & hit : injury.stacks)
			EXPECT_FALSE(hit.shattered());
}

TEST_F(NewHorizonsCureTest, HandOfFateZeroImmediateSpillThawsWithoutShatterOrReroll)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1000, 1000));
	const SpellID hand(SpellID::decode("new-horizons:handOfFate"));
	ASSERT_TRUE(hand.hasValue());
	attackerSideHero->addSpellToSpellbook(hand);
	// 100% authored MDR still leaves 5% under the canonical 95% cap.
	// This supported 1% final cap instead floors to zero for a Pikeman.
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::DAMAGE_RECEIVED_CAP, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(target->getMaxHealth() / 100, 0);
	ASSERT_NO_FATAL_FAILURE(addFrozen(target));
	const auto * spell = hand.toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	const auto primaryDamage = mechanics->adjustEffectValue(poisonEnemy);
	ASSERT_GT(primaryDamage, 0);
	ASSERT_EQ(mechanics->adjustRecipientDamage(target, primaryDamage / 2), 0);
	const auto stamp = target->frozenLastAppliedRound();
	const auto friendlyHP = target->getAvailableHealth();
	const auto enemyHP = poisonEnemy->getAvailableHealth();
	CRandomGenerator expected(seed);
	for(size_t recipient = 0; recipient < battle()->battleGetAllUnits(false).size(); ++recipient)
		expected.nextInt(0, 99);
	expected.nextInt(1, 1);
	gameHandler->randomizer->setSeed(seed);
	auto action = cureAction(poisonEnemy);
	action.spell = hand;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_GT(enemyHP - poisonEnemy->getAvailableHealth(), 0);
	EXPECT_EQ(target->getAvailableHealth(), friendlyHP);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), stamp);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expected.nextInt());
	ASSERT_EQ(server.castsOf(hand).size(), 1u);
	ASSERT_FALSE(server.injuries.empty());
	for(const auto & injury : server.injuries)
		for(const auto & hit : injury.stacks)
			EXPECT_FALSE(hit.shattered());
}

TEST_F(NewHorizonsCureTest, CanonicalPoisonIsAvailableAsLevelTwoNatureAtSevenMana)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	const SpellID poison = naturePoisonSpell();
	ASSERT_TRUE(newHorizonsMagic::physicalPoisonEnabled(rules, poison));
	EXPECT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(rules, poison));
	EXPECT_FALSE(newHorizonsMagic::physicalPoisonEnabled(rules, SpellID(SpellID::POISON)));
	const auto * originalAbility = SpellID(SpellID::POISON).toSpell();
	ASSERT_NE(originalAbility, nullptr);
	EXPECT_TRUE(originalAbility->isCreatureAbility());
	EXPECT_FALSE(originalAbility->isCommonHeroSpell());
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, poison), 2);
	const auto schools = newHorizonsMagic::spellSchools(rules, poison);
	ASSERT_EQ(schools.size(), 1u);
	EXPECT_EQ(schools.front().serializationKey(), "new-horizons:nature");
	EXPECT_TRUE(newHorizonsMagic::mageGuildGenerationActive(rules));
	EXPECT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, poison));
	EXPECT_TRUE(vstd::contains(newHorizonsMagic::preferredSchools(rules, FactionID::RAMPART), schools.front()));
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, poison, rank), 7);

	constexpr std::array<int, 4> coefficients{100, 115, 130, 145};
	constexpr std::array<int64_t, 4> baseDamage{70, 77, 85, 92};
	for(int rank = 0; rank < 4; ++rank)
	{
		EXPECT_EQ(newHorizonsMagic::schoolRankPowerCoefficientPercent(rules, rank), coefficients[rank]);
		EXPECT_EQ(newHorizonsMagic::poisonBaseDamage(100, coefficients[rank]), baseDamage[rank]);
	}
	EXPECT_EQ(newHorizonsMagic::poisonBaseDamage(101, 115), 78);
}

TEST_F(NewHorizonsCureTest, OldV2RosterWithoutTheNewPoisonSpellDoesNotAcquireIt)
{
	auto rules = savedV2CureRules(JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	rules["spells"].Struct().erase(std::string(newHorizonsMagic::NATURE_POISON_SPELL));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(rules, naturePoisonSpell()));
	EXPECT_FALSE(newHorizonsMagic::physicalPoisonEnabled(rules, naturePoisonSpell()));
}

TEST_F(NewHorizonsCureTest, V3PoisonUsesTheComposedSchoolAndSpellcraftCoefficient)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	const auto poison = naturePoisonSpell();
	attackerSideHero->addSpellToSpellbook(poison);
	const int natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, poison), 12650);
	EXPECT_EQ(newHorizonsMagic::poisonBaseDamageBasisPoints(0, 12650), 20)
		<< "Spellcraft leaves Poison's fixed 20 damage unchanged";
	const int manaBefore = attackerSideHero->getManaAvailable();
	const auto healthBefore = poisonEnemy->getAvailableHealth();

	const auto * spell = poison.toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	EXPECT_EQ(mechanics->getRangeLevel(), 0);
	EXPECT_FALSE(mechanics->isMassive());

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 83);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, -1);
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(poisonEnemy->acquireState().get()), 83);
	EXPECT_TRUE(poisonEnemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(poison)))->empty());
	EXPECT_EQ(poisonEnemy->getAvailableHealth(), healthBefore);
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 7);
}

TEST_F(NewHorizonsCureTest, V3PoisonCastTicksOnThreeRealActivationsAndExpires)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1000, 1000));
	const auto poison = naturePoisonSpell();
	attackerSideHero->addSpellToSpellbook(poison);
	const int natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	ASSERT_EQ(poisonEnemy->physicalPoisonBaseDamage, 77);
	ASSERT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 3);
	const auto initialHealth = poisonEnemy->getAvailableHealth();
	const auto firstInjuryIndex = server.injuries.size();
	constexpr std::array<int64_t, 3> expectedDamage{77, 115, 154};
	int64_t totalDamage = 0;

	for(size_t activation = 0; activation < expectedDamage.size(); ++activation)
	{
		ASSERT_TRUE(advanceUntilPhysicalPoisonActivation(poisonEnemy,
			static_cast<int32_t>(expectedDamage.size() - activation)));
		ASSERT_GE(server.injuries.size(), firstInjuryIndex);
		std::vector<int64_t> poisonHits;
		for(size_t injuryIndex = firstInjuryIndex; injuryIndex < server.injuries.size(); ++injuryIndex)
			for(const auto & hit : server.injuries[injuryIndex].stacks)
				if(hit.stackAttacked == poisonEnemy->unitId())
					poisonHits.push_back(hit.damageAmount);

		ASSERT_EQ(poisonHits.size(), activation + 1);
		EXPECT_EQ(poisonHits.back(), expectedDamage[activation]);
		totalDamage += expectedDamage[activation];
		EXPECT_EQ(poisonEnemy->getAvailableHealth(), initialHealth - totalDamage);
		EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining,
			static_cast<int32_t>(expectedDamage.size() - activation - 1));
	}

	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, -1);
}

TEST_F(NewHorizonsCureTest, V3EqualPotencyPoisonCastRefreshesTheActivationCount)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	const int natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	addPhysicalPoison(77, 2, poisonEnemy, 15);
	ASSERT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 2);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 77);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, -1);
}

TEST_F(NewHorizonsCureTest, V3StrongerPoisonCastRefreshesTheCurrentAffliction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	const int natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	addPhysicalPoison(70, 2, poisonEnemy, 15);
	ASSERT_EQ(poisonEnemy->physicalPoisonBaseDamage, 70);
	ASSERT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 2);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 77);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, -1);
}

TEST_F(NewHorizonsCureTest, V3WeakerPoisonCastLeavesTheStrongerCurrentAfflictionUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	addPhysicalPoison(70, 2, poisonEnemy, 15);
	ASSERT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 2);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 70);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 2);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, 15);
}

TEST_F(NewHorizonsCureTest, V3PoisonRejectsFriendlyAndNonLivingTargetsWithoutSpendingTheCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	const int manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	poisonEnemy->addNewBonus(spellEffect(SpellID::POISON, BonusType::UNDEAD, 0));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 0);
}

TEST_F(NewHorizonsCureTest, V3PoisonRejectsDeadTargetsWithoutSpendingTheCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	const int manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_NO_FATAL_FAILURE(injure(poisonEnemy->getAvailableHealth(), poisonEnemy));
	ASSERT_FALSE(poisonEnemy->alive());

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 0);
}

TEST_F(NewHorizonsCureTest, V2RosterCustomPoisonUsesTheFallbackTimedEffect)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	EXPECT_EQ(battle()->getBattle()->getMagicRules()["rulesetVersion"].Integer(), magicVersion);
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(battle()->getBattle()->getMagicRules()));
	ASSERT_NO_THROW(newHorizonsHeroes::startingBookReplacement(attackerSideHero->getPrimaryGrowthRules(),
		battle()->getBattle()->getMagicRules(), HeroTypeID::NONE, SpellID::NONE));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	EXPECT_FALSE(newHorizonsMagic::physicalPoisonEnabled(battle()->getBattle()->getMagicRules(),
		naturePoisonSpell()));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_FALSE(poisonEnemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(naturePoisonSpell())))->empty());
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 0);
}

TEST_F(NewHorizonsCureTest, CanonicalProfileOptsIntoOnlyPoisonAndDiseaseAtFourManaEveryRank)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	const auto & cure = rules["spells"][cureKey];
	ASSERT_EQ(cure["costs"].Vector().size(), 4u);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, SpellID::CURE, rank), 4);
	ASSERT_EQ(cure["cureAfflictions"].Vector().size(), 2u);
	EXPECT_EQ(cure["cureAfflictions"].Vector()[0].String(), "core:poison");
	EXPECT_EQ(cure["cureAfflictions"].Vector()[1].String(), "core:disease");
	EXPECT_TRUE(newHorizonsMagic::cureEnabled(rules, SpellID::CURE));
	EXPECT_FALSE(newHorizonsMagic::cureEnabled(rules, SpellID::SLOW));
}

TEST_F(NewHorizonsCureTest, OptedInDescriptionExplainsReworkedCure)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(newHorizonsMagic::cureEnabled(attackerSideHero->getMagicRules(), spell->getId()));
	const auto description = newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, 0);

	EXPECT_NE(description.find("one friendly living stack"), std::string::npos);
	EXPECT_NE(description.find("25 + 1.5"), std::string::npos);
	EXPECT_NE(description.find("cannot resurrect casualties"), std::string::npos);
	EXPECT_NE(description.find("Poison or Disease"), std::string::npos);
	EXPECT_NE(description.find("one physical affliction"), std::string::npos);
	EXPECT_NE(description.find("does not remove magical effects"), std::string::npos);
	EXPECT_NE(description, spell->getDescriptionTranslated(0));
}

TEST_F(NewHorizonsCureTest, MissingSavedCureOptInKeepsTranslatedCoreDescription)
{
	optIntoNewCure = false;
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_FALSE(newHorizonsMagic::cureEnabled(attackerSideHero->getMagicRules(), spell->getId()));

	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, 0),
		spell->getDescriptionTranslated(0));
}

TEST_F(NewHorizonsCureTest, OptionalSavedRowFieldPreservesLegacyMechanicsAndRejectsInvalidGroups)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["spells"][cureKey].Struct().erase("cureAfflictions");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::cureEnabled(rules, SpellID::CURE));

	rules["spells"][cureKey]["cureAfflictions"].Vector().emplace_back(JsonNode("core:slow"));
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	JsonNode nonCure(JsonPath::builtin("config/newHorizonsMagic"));
	nonCure["spells"]["core:haste"]["cureAfflictions"].Vector().emplace_back(JsonNode("core:poison"));
	EXPECT_THROW(newHorizonsMagic::validateRules(nonCure), std::runtime_error);
}

TEST_F(NewHorizonsCureTest, BattleActionSelectionRoundTripsAndCannotBeDiscardedByOldProtocol)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CURE;
	action.spellCureAffliction = SpellID::DISEASE;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.spellCureAffliction, SpellID::DISEASE);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CHAIN_GATE;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsCureTest, HealsByTwentyFivePlusFloorOnePointFiveSpellPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare(21, "core:archangel", 10));
	ASSERT_NO_FATAL_FAILURE(injure(100));
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&parameters)->getEffectValue(), 56);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cureAction(target)));
	EXPECT_EQ(target->getAvailableHealth() - before, 56);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
	EXPECT_EQ(target->getCount(), 10);
}

TEST_F(NewHorizonsCureTest, BasicSchoolAndSpellcraftScaleOnlyCuresSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare(23, "core:archangel", 10));
	const SecondarySkill light(SecondarySkill::decode("new-horizons:lightMagic"));
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(injure(100));

	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, SpellID::CURE), 12650);
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), 68)
		<< "The fixed 25 HP remains unchanged while the 1.5 × Spell Power term uses 126.5%";

	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cureAction(target)));
	EXPECT_EQ(target->getAvailableHealth() - before, 68);
}

TEST_F(NewHorizonsCureTest, HealerPerkRaisesOnlyCuresSpellPowerHealing)
{
	enableHealerPerkRules = true;
	ASSERT_NO_FATAL_FAILURE(prepare(20, "core:archangel", 10));
	const auto lightSkill = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
	attackerSideHero->setSecSkillLevel(lightSkill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast before(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto ordinaryHealing = spell->battleMechanics(&before)->getEffectValue();
	ASSERT_EQ(ordinaryHealing, 59);

	attackerSideHero->applyPerkSelection(
		{"new-horizons:lightMagic", "new-horizons:lightMagic.healer"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:lightMagic", "new-horizons:lightMagic.healer"));
	spells::BattleCast withPerk(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&withPerk)->getEffectValue(), 65)
		<< "Only the 34 HP Spell Power-derived component gains 20%; the fixed 25 stays 25";
}

TEST_F(NewHorizonsCureTest, FullHealthTargetCanBeCleansedBySelectingPoison)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPoison();
	ASSERT_TRUE(target->getAvailableHealth() >= target->getTotalHealth());
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(NewHorizonsCureTest, CureSelectsAndRemovesPhysicalOnlyPoison)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPhysicalPoison(5, 2);
	ASSERT_GE(target->getAvailableHealth(), target->getTotalHealth());
	const auto afflictions = newHorizonsMagic::cureAfflictions(
		attackerSideHero->getMagicRules(), target);
	ASSERT_TRUE(vstd::contains(afflictions, SpellID::POISON));
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::DISEASE)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(target->physicalPoisonSourceStackId, -1);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(NewHorizonsCureTest, CureRemovesBothMagicalAndPhysicalPoisonWhenSelected)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPoison();
	addPhysicalPoison(5, 2);
	const auto mana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(target->physicalPoisonSourceStackId, -1);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(NewHorizonsCureTest, FullHealthSurvivorIsNotHealableForItsCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 2));
	CStack * casualtyStack = target;
	ASSERT_NO_FATAL_FAILURE(injure(casualtyStack->getMaxHealth()));
	ASSERT_EQ(casualtyStack->getCount(), 1);
	ASSERT_EQ(casualtyStack->getFirstHPleft(), casualtyStack->getMaxHealth());
	ASSERT_LT(casualtyStack->getAvailableHealth(), casualtyStack->getTotalHealth());

	auto * injuredAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 1);
	ASSERT_NE(injuredAlly, nullptr);
	ASSERT_NO_FATAL_FAILURE(injure(2, injuredAlly));

	const auto * cure = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(cure, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, cure);
	const auto mechanics = cure->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(casualtyStack)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(casualtyStack->getCount(), 1);
	EXPECT_EQ(casualtyStack->getFirstHPleft(), casualtyStack->getMaxHealth());
}

TEST_F(NewHorizonsCureTest, SpellLockedPhysicalPoisonTargetIsRejectedWithoutSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPhysicalPoison();
	auto * injuredAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 1);
	ASSERT_NE(injuredAlly, nullptr);
	ASSERT_NO_FATAL_FAILURE(injure(2, injuredAlly));

	const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	ASSERT_NE(spellLock, SpellID::NONE);
	target->addNewBonus(spellEffect(spellLock, BonusType::MAGIC_RESISTANCE, 100));
	const auto afflictions = newHorizonsMagic::cureAfflictions(
		attackerSideHero->getMagicRules(), target);
	ASSERT_TRUE(vstd::contains(afflictions, SpellID::POISON));

	const auto * cure = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(cure, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, cure);
	const auto mechanics = cure->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 3);
}

TEST_F(NewHorizonsCureTest, OrdinaryDispelDoesNotRemovePhysicalPoison)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPoison();
	addPhysicalPoison();
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::DISPEL;
	action.aimToUnit(target);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 5);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 3);
}

TEST_F(NewHorizonsCureTest, PoisonHealthReductionIsRemovedBeforeHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 10));
	const auto fullTotalHealth = target->getTotalHealth();
	addPoison(true);
	const auto poisonedTotalHealth = target->getTotalHealth();
	ASSERT_LT(poisonedTotalHealth, fullTotalHealth);
	// Keep all ten pikemen alive. Cure's ordinary HEAL effect cannot resurrect
	// casualties and can only fill the wound on the first surviving creature.
	ASSERT_NO_FATAL_FAILURE(injure(8));
	const auto healthBeforeCure = target->getAvailableHealth();
	ASSERT_LT(target->getAvailableHealth(), target->getTotalHealth());
	ASSERT_EQ(target->getCount(), 10);
	ASSERT_EQ(target->getMaxHealth(), 9);
	ASSERT_EQ(target->getFirstHPleft(), 2);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_EQ(target->getTotalHealth(), fullTotalHealth);
	EXPECT_EQ(target->getCount(), 10);
	// Under Poison, the stack has nine full units at 9 HP and a first unit at
	// 2 HP. Removing the health penalty first restores those nine units (+9),
	// then HEAL fills the first unit by 8, to 100 HP. Reversing the order would
	// cap HEAL at 7 (9 - 2), then restore the penalty, ending at 99 instead.
	EXPECT_EQ(target->getAvailableHealth() - healthBeforeCure, 17);
}

TEST_F(NewHorizonsCureTest, SelectedDiseaseRemovesItsWholeGroupAndLeavesPoisonAlone)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPoison();
	addDisease();
	const auto afflictions = newHorizonsMagic::cureAfflictions(
		battle()->getBattle()->getMagicRules(), target);
	ASSERT_EQ(afflictions.size(), 2u);
	EXPECT_EQ(afflictions[0], SpellID::POISON);
	EXPECT_EQ(afflictions[1], SpellID::DISEASE);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::DISEASE)));
	EXPECT_FALSE(hasSource(SpellID::DISEASE));
	EXPECT_TRUE(hasSource(SpellID::POISON));
}

TEST_F(NewHorizonsCureTest, MagicalConditionsAreNotCureAfflictions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addMagicalConditions();
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto mana = attackerSideHero->getManaAvailable();

	for(const auto spell : {SpellID::CURSE, SpellID::SLOW, SpellID::BERSERK})
	{
		EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			cureAction(target, spell)));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
		EXPECT_TRUE(hasSource(spell));
	}
}

TEST_F(NewHorizonsCureTest, SuccessfulCurePreservesStoneGazeAndOtherMagicalConditions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addPoison();
	addMagicalConditions();
	target->addNewBonus(spellEffect(SpellID::STONE_GAZE, BonusType::NOT_ACTIVE, 0));
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto mana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	for(const auto spell : {SpellID::STONE_GAZE, SpellID::CURSE, SpellID::SLOW, SpellID::BERSERK})
		EXPECT_TRUE(hasSource(spell));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(NewHorizonsCureTest, StaleSelectorIsRejectedBeforeManaAndHeroActionAreSpent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	const SpellID forged(10000);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, forged)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsCureTest, HealOnlySelectorCannotBypassARequiredAfflictionChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addPoison();
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(hasSource(SpellID::POISON));
}

TEST_F(NewHorizonsCureTest, NonCureSpellRejectsCureSelectorBeforeSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(2));
	attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
	const auto mana = attackerSideHero->getManaAvailable();
	auto hasteAction = cureAction(target, SpellID::POISON);
	hasteAction.spell = SpellID::HASTE;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), hasteAction));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}


TEST_F(NewHorizonsCureTest, LegacyCureRejectsAfflictionSelectorBeforeSpendingMana)
{
	optIntoNewCure = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto legacyMana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), legacyMana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsCureTest, EnemyAndUndeadTargetsAreRejectedWithoutSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(2)); // keeps the generic spell available before target selection
	const auto mana = attackerSideHero->getManaAvailable();
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 1);
	enemy->addNewBonus(spellEffect(SpellID::POISON, BonusType::POISON, 30));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(enemy, SpellID::POISON)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_FALSE(enemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::POISON))))->empty());

	auto * undead = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(4, 5), 1);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(undead)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsCureTest, ExpertCureStaysSingleTargetAndNeverResurrects)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto lightMagic = SecondarySkill::decode("new-horizons:lightMagic");
	ASSERT_GE(lightMagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::CURE);
	startBattle();
	removeDeployedUnits();
	target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 2);
	auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	beginCombat();
	addPoison();
	other->addNewBonus(spellEffect(SpellID::POISON, BonusType::POISON, 30));
	const auto otherHealth = other->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(injure(16)); // one casualty; the remaining living creature can be healed, not resurrected
	ASSERT_EQ(target->getCount(), 1);

	const auto * spell = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	EXPECT_EQ(mechanics->getRangeLevel(), 0);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(target->getCount(), 1);
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_TRUE(!other->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::POISON))))->empty());
	EXPECT_EQ(other->getAvailableHealth(), otherHealth);
}

TEST_F(NewHorizonsCureTest, UlandSpellSpecialtyStillScalesTheHealingComponent)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto ulandId = HeroTypeID::decode("core:uland");
	ASSERT_GE(ulandId, 0);
	attackerSideHero->setHeroType(HeroTypeID(ulandId));
	for(const auto & bonus : attackerSideHero->getHeroType()->specialty)
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(*bonus));
	attackerSideHero->level = 7;
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::CURE);
	startBattle();
	removeDeployedUnits();
	target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(3, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	beginCombat();
	ASSERT_NO_FATAL_FAILURE(injure(100));

	const auto baseHealing = int64_t{25} + (int64_t{3} * 20) / 2;
	const auto expectedHealing = attackerSideHero->getSpellBonus(SpellID(SpellID::CURE).toSpell(),
		baseHealing, target);
	ASSERT_GT(expectedHealing, baseHealing);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cureAction(target)));
	EXPECT_EQ(target->getAvailableHealth() - before, expectedHealing);
}

TEST_F(NewHorizonsCureTest, SavedProfileWithoutTheOptInKeepsOriginalCureMechanics)
{
	optIntoNewCure = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	EXPECT_FALSE(mechanics->isNewHorizonsCure());
	EXPECT_FALSE(newHorizonsMagic::cureEnabled(
		battle()->getBattle()->getMagicRules(), SpellID::CURE));
}

TEST_F(NewHorizonsCureTest, GraveDustChoiceDoesNotActivateOnHistoricalCureSnapshot)
{
	optIntoNewCure = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect effects;
	effects.battleID = BattleID(0);
	effects.toAdd.emplace_back(target->unitId(), newHorizonsGraveDust::records(5000, PlayerColor(1)));
	gameHandler->sendAndApply(effects);
	const auto * cure = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, cure);
	cast.setCurePhysicalAffliction("graveDust");
	EXPECT_FALSE(cure->battleMechanics(&cast)->canBeCastAt({spells::Destination(target)}));
	EXPECT_EQ(spells::curePhysical::graveDustRemoval(*target).size(), 2u);
}
