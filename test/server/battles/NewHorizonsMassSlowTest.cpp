/*
 * NewHorizonsMassSlowTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/CStack.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include <vcmi/Environment.h>

#include <memory>
#include <set>
#include <string>
#include <utility>

namespace
{
constexpr auto massSlowKey = "new-horizons:massSlow";
constexpr auto spellLockKey = "new-horizons:spellLock";
constexpr auto sorcerySkillKey = "new-horizons:sorceryMagic";
constexpr auto temporalistPerkKey = "new-horizons:sorceryMagic.temporalist";
constexpr auto temporalFieldPerkKey = "new-horizons:sorceryMagic.temporalField";

SpellID massSlowSpell()
{
	return SpellID(SpellID::decode(massSlowKey));
}

SpellID spellLockSpell()
{
	return SpellID(SpellID::decode(spellLockKey));
}

JsonNode olderV3MagicSnapshotWithoutMassSlow()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::CURRENT_RULESET_VERSION;
	rules["spells"].Struct().erase(massSlowKey);
	return rules;
}

class MassSlowPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MassSlowPredictionEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsMassSlowTest : public HeroCommandFixture
{
protected:
	bool oldV3Snapshot = false;
	CStack * friendly = nullptr;
	CStack * firstEnemy = nullptr;
	CStack * secondEnemy = nullptr;
	CStack * baseImmuneEnemy = nullptr;
	CStack * variantImmuneEnemy = nullptr;
	CStack * spellLockedEnemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(massSlowSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);

		JsonNode magicRules = oldV3Snapshot
			? olderV3MagicSnapshotWithoutMassSlow()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		if(!oldV3Snapshot)
			magicRules["spells"][massSlowKey]["active"] = JsonNode(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][sorcerySkillKey]["perks"].Vector())
			if(perk["id"].String() == temporalFieldPerkKey)
				perk["effect"]["status"] = JsonNode("active");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void prepareHero(bool selectTemporalField, int sorceryRank = MasteryLevel::ADVANCED,
		int spellPower = 100, int wisdomRank = MasteryLevel::NONE, int mana = 1000)
	{
		startGame();
		attackerSideHero->removeAllSpells();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);

		const SecondarySkill sorcery(SecondarySkill::decode(sorcerySkillKey));
		ASSERT_TRUE(sorcery.hasValue());
		attackerSideHero->setSecSkillLevel(sorcery, sorceryRank, ChangeValueMode::ABSOLUTE);
		if(wisdomRank > MasteryLevel::NONE)
		{
			const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
			ASSERT_TRUE(wisdom.hasValue());
			attackerSideHero->setSecSkillLevel(wisdom, wisdomRank, ChangeValueMode::ABSOLUTE);
		}
		setTestSpellPointTotal(attackerSideHero, mana);

		if(selectTemporalField)
			selectTemporalFieldWithPrerequisites(sorcery, sorceryRank);
	}

	void selectTemporalFieldWithPrerequisites(const SecondarySkill & sorcery, int finalRank)
	{
		attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({sorcerySkillKey, temporalistPerkKey});
		attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({sorcerySkillKey, temporalFieldPerkKey});
		attackerSideHero->setSecSkillLevel(sorcery, finalRank, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->hasActivePerk(sorcerySkillKey, temporalFieldPerkKey));
	}

	void prepareBattle()
	{
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);

		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
		firstEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 5), 10);
		secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(12, 5), 10);
		baseImmuneEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:griffin"), BattleHex(14, 5), 10);
		variantImmuneEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 7), 10);
		spellLockedEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(16, 5), 10);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(firstEnemy, nullptr);
		ASSERT_NE(secondEnemy, nullptr);
		ASSERT_NE(baseImmuneEnemy, nullptr);
		ASSERT_NE(variantImmuneEnemy, nullptr);
		ASSERT_NE(spellLockedEnemy, nullptr);
		addSpellImmunity(baseImmuneEnemy, SpellID(SpellID::SLOW));
		addSpellImmunity(variantImmuneEnemy, massSlowSpell());
		addSpellLock(spellLockedEnemy);
		beginCombat();
		activateAttackerStack();
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
		EXPECT_EQ(newHorizonsMagic::hasDistinctMassSlow(battle()->getMagicRules()), !oldV3Snapshot);
	}

	void activateAttackerStack()
	{
		ASSERT_NE(friendly, nullptr);
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = friendly->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	void advanceRoundForAttackerAction()
	{
		const int roundBefore = battle()->getRound();
		advanceRound();
		ASSERT_EQ(battle()->getRound(), roundBefore + 1);
		activateAttackerStack();
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	void addSpellImmunity(CStack * unit, SpellID spell)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell)));
	}

	void addSpellLock(CStack * unit)
	{
		auto lock = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
			BonusSource::SPELL_EFFECT, 100, BonusSourceID(spellLockSpell()));
		lock->turnsRemain = 1;
		unit->addNewBonus(std::move(lock));
	}

	void addSlowSpecialty(int value)
	{
		auto specialty = std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPECIAL_ADD_VALUE_ENCHANT, BonusSource::OTHER, 0, BonusSourceID(),
			BonusSubtypeID(SpellID(SpellID::SLOW)));
		specialty->parameters = std::make_shared<BonusParameters>(value);
		attackerSideHero->addNewBonus(std::move(specialty));
	}

	spells::Target massAim() const
	{
		return {spells::Destination(BattleHex::INVALID)};
	}

	bool castSpell(SpellID spell, const CStack * selectedTarget = nullptr)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		if(selectedTarget)
			action.aimToUnit(selectedTarget);
		else
			action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castLegacySlowToggle()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::SLOW;
		action.spellMassSlow = true;
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	std::shared_ptr<const Bonus> slowEffect(const battle::Unit * unit) const
	{
		if(!unit)
			return {};
		const auto bonuses = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW)))
			.And(Selector::type()(BonusType::STACKS_INITIATIVE)));
		return bonuses && !bonuses->empty() ? bonuses->front() : std::shared_ptr<const Bonus>{};
	}

	size_t slowEffectCount(const battle::Unit * unit) const
	{
		if(!unit)
			return 0;
		const auto bonuses = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW)))
			.And(Selector::type()(BonusType::STACKS_INITIATIVE)));
		return bonuses ? bonuses->size() : 0;
	}

	std::set<uint32_t> affectedIds(const spells::Mechanics & mechanics, const spells::Target & aim) const
	{
		std::set<uint32_t> result;
		for(const auto * unit : mechanics.getAffectedStacks(aim))
			if(unit)
				result.insert(unit->unitId());
		return result;
	}

	void expectVirtualMassSlowSource(bool present)
	{
		const auto variant = massSlowSpell();
		EXPECT_EQ(attackerSideHero->getSpellsInSpellbook().contains(variant), present);
		EXPECT_EQ(attackerSideHero->getInscribedSpellsForCasting().contains(variant), present);
		EXPECT_EQ(attackerSideHero->canCastThisSpell(variant.toSpell()), present);
		EXPECT_EQ(!attackerSideHero->getSourcesForSpell(variant).empty(), present);
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(variant))
			<< "Temporal Field grants a virtual Mass Slow source without durably inscribing the variant";
	}
};

TEST_F(NewHorizonsMassSlowTest, TemporalFieldGrantsAVirtualSpellOnlyAfterTemporalistAndWithAPhysicalSpellbook)
{
	JsonNode defaultRules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_TRUE(defaultRules["spells"][massSlowKey].isStruct());
	EXPECT_TRUE(defaultRules["spells"][massSlowKey]["active"].Bool())
		<< "The verified distinct variant is enabled for newly created profiles";

	prepareHero(false, MasteryLevel::BASIC);
	const auto variant = massSlowSpell();
	const auto & rules = attackerSideHero->getMagicRules();
	EXPECT_TRUE(newHorizonsMagic::hasDistinctMassSlow(rules));
	EXPECT_EQ(newHorizonsMagic::spellVariantBase(rules, variant), SpellID(SpellID::SLOW));
	EXPECT_EQ(newHorizonsMagic::spellVariantPowerPercent(rules, variant), 60);
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, variant));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(variant.toSpell(), true));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(rules, attackerSideHero, variant));
	expectVirtualMassSlowSource(false);

	const SecondarySkill sorcery(SecondarySkill::decode(sorcerySkillKey));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_THROW(attackerSideHero->applyPerkSelection({sorcerySkillKey, temporalFieldPerkKey}), std::runtime_error)
		<< "Temporal Field cannot be selected before the earlier Basic Temporalist perk";
	selectTemporalFieldWithPrerequisites(sorcery, MasteryLevel::ADVANCED);
	EXPECT_TRUE(newHorizonsMagic::variantGrantAvailable(rules, attackerSideHero, variant));
	expectVirtualMassSlowSource(true);

	attackerSideHero->removeArtifact(ArtifactPosition::SPELLBOOK);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(sorcerySkillKey, temporalFieldPerkKey));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(rules, attackerSideHero, variant));
	expectVirtualMassSlowSource(false);
}

TEST_F(NewHorizonsMassSlowTest, LosingAdvancedSorceryRevokesTemporalFieldsVirtualMassSlowSource)
{
	prepareHero(true, MasteryLevel::ADVANCED);
	const SecondarySkill sorcery(SecondarySkill::decode(sorcerySkillKey));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(sorcerySkillKey, temporalFieldPerkKey));
	expectVirtualMassSlowSource(true);

	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(sorcerySkillKey, temporalFieldPerkKey));
	expectVirtualMassSlowSource(false);
}

TEST_F(NewHorizonsMassSlowTest, ManualAndScrollAcquisitionCannotGrantMassSlow)
{
	prepareHero(false);
	const auto variant = massSlowSpell();
	const auto & rules = attackerSideHero->getMagicRules();
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, variant));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(variant.toSpell(), true));

	attackerSideHero->addSpellToSpellbook(variant);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, variant, ArtifactPosition::FIRST_AVAILABLE));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(variant));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
	EXPECT_FALSE(attackerSideHero->getSpellsInSpellbook().contains(variant));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
}

TEST_F(NewHorizonsMassSlowTest, ExpertOrdinarySlowRemainsFullStrengthAndSingleTarget)
{
	prepareHero(true, MasteryLevel::EXPERT, 300);
	prepareBattle();
	addSlowSpecialty(-10);

	const auto * base = SpellID(SpellID::SLOW).toSpell();
	spells::BattleCast baseCast(battle(), attackerSideHero, spells::Mode::HERO, base);
	const auto baseMechanics = base->battleMechanics(&baseCast);
	EXPECT_EQ(baseMechanics->getEffectLevel(), MasteryLevel::EXPERT);
	EXPECT_FALSE(baseMechanics->isMassive());
	EXPECT_EQ(affectedIds(*baseMechanics, {spells::Destination(firstEnemy)}),
		(std::set<uint32_t>{firstEnemy->unitId()}));

	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castSpell(SpellID::SLOW, firstEnemy));
	ASSERT_NE(slowEffect(firstEnemy), nullptr);
	EXPECT_EQ(slowEffect(firstEnemy)->val, -60)
		<< "The ordinary 50% cap applies before the -10 target-specific Slow specialty";
	EXPECT_EQ(slowEffect(firstEnemy)->turnsRemain, 3)
		<< "Expert Slow keeps its two-round base plus the earlier Temporalist perk";
	EXPECT_EQ(slowEffect(secondEnemy), nullptr)
		<< "Expert mastery and Temporal Field do not convert the ordinary Slow entry to Mass Slow";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 3);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(NewHorizonsMassSlowTest, MassSlowScalesTheFinalCappedSpecializedSlowAndMatchesDetachedFamilyRefresh)
{
	prepareHero(true, MasteryLevel::EXPERT, 300, MasteryLevel::EXPERT);
	prepareBattle();
	addSlowSpecialty(-10);

	const auto variant = massSlowSpell();
	const auto * definition = variant.toSpell();
	ASSERT_NE(definition, nullptr);
	const auto & rules = battle()->getBattle()->getMagicRules();
	EXPECT_EQ(newHorizonsMagic::spellVariantBase(rules, variant), SpellID(SpellID::SLOW));
	EXPECT_EQ(newHorizonsMagic::spellVariantPowerPercent(rules, variant), 60);
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, variant));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(definition, true));

	for(const int mastery : {MasteryLevel::NONE, MasteryLevel::BASIC,
		MasteryLevel::ADVANCED, MasteryLevel::EXPERT})
	{
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, variant, mastery),
			3 * newHorizonsMagic::spellCost(rules, SpellID(SpellID::SLOW), mastery))
			<< "Mass Slow's listed Mana cost is triple Slow before Wisdom at rank " << mastery;
	}
	const int listedCost = newHorizonsMagic::spellCost(rules, variant, MasteryLevel::EXPERT);
	EXPECT_EQ(listedCost, 9);
	const int expectedManaCost = newHorizonsMagic::wisdomAdjustedCost(
		listedCost, 1, MasteryLevel::EXPERT);
	EXPECT_EQ(battle()->battleGetSpellCost(definition, attackerSideHero), expectedManaCost)
		<< "Wisdom discounts the already-tripled listed cost";

	spells::BattleCast variantCast(battle(), attackerSideHero, spells::Mode::HERO, definition);
	const auto variantMechanics = definition->battleMechanics(&variantCast);
	EXPECT_TRUE(variantMechanics->isMassive());
	EXPECT_EQ(variantMechanics->getEffectLevel(), MasteryLevel::EXPERT);
	EXPECT_EQ(variantMechanics->getEffectDuration(), 3)
		<< "Mass Slow retains Slow's two-round base plus Temporalist's one additional round";
	EXPECT_EQ(affectedIds(*variantMechanics, massAim()),
		(std::set<uint32_t>{firstEnemy->unitId(), secondEnemy->unitId()}));

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	MassSlowPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	Bonus priorSlow(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE,
		BonusSource::SPELL_EFFECT, -10, BonusSourceID(SpellID(SpellID::SLOW)));
	priorSlow.turnsRemain = 1;
	firstEnemy->addNewBonus(std::make_shared<Bonus>(priorSlow));
	projected.addUnitBonus(firstEnemy->unitId(), {priorSlow});
	const int realManaBefore = attackerSideHero->getManaAvailable();
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, definition);
	const auto projectedMechanics = definition->battleMechanics(&projectedCast);
	ASSERT_TRUE(projectedMechanics->isMassive());
	projectedMechanics->castEval(projected.getServerCallback(), massAim());

	const auto * projectedFirst = projected.battleGetUnitByID(firstEnemy->unitId());
	const auto * projectedSecond = projected.battleGetUnitByID(secondEnemy->unitId());
	const auto * projectedFriendly = projected.battleGetUnitByID(friendly->unitId());
	const auto * projectedBaseImmune = projected.battleGetUnitByID(baseImmuneEnemy->unitId());
	const auto * projectedVariantImmune = projected.battleGetUnitByID(variantImmuneEnemy->unitId());
	const auto * projectedSpellLocked = projected.battleGetUnitByID(spellLockedEnemy->unitId());
	ASSERT_NE(slowEffect(projectedFirst), nullptr);
	ASSERT_NE(slowEffect(projectedSecond), nullptr);
	ASSERT_EQ(slowEffectCount(projectedFirst), 1u)
		<< "Detached AI refreshes an already-materialized Slow family effect";
	EXPECT_EQ(slowEffect(projectedFirst)->val, -36)
		<< "Ordinary Slow is capped at -50, specialty makes -60, then the variant applies 60%";
	EXPECT_EQ(slowEffect(projectedSecond)->val, -36);
	EXPECT_EQ(slowEffect(projectedFirst)->turnsRemain, 3);
	EXPECT_EQ(slowEffect(projectedSecond)->turnsRemain, 3);
	EXPECT_EQ(slowEffect(projectedFriendly), nullptr);
	EXPECT_EQ(slowEffect(projectedBaseImmune), nullptr);
	EXPECT_EQ(slowEffect(projectedVariantImmune), nullptr);
	EXPECT_EQ(slowEffect(projectedSpellLocked), nullptr);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), realManaBefore)
		<< "Detached prediction does not spend the authoritative hero's Mana";

	ASSERT_TRUE(castSpell(variant));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), realManaBefore - expectedManaCost);
	for(const CStack * unit : {firstEnemy, secondEnemy})
	{
		ASSERT_NE(slowEffect(unit), nullptr);
		EXPECT_EQ(slowEffect(unit)->val, -36);
		EXPECT_EQ(slowEffect(unit)->turnsRemain, 3);
	}
	EXPECT_EQ(slowEffectCount(firstEnemy), 1u)
		<< "Accepted cast refreshes the family-scoped Slow instead of duplicating it";
	EXPECT_EQ(slowEffect(friendly), nullptr);
	EXPECT_EQ(slowEffect(baseImmuneEnemy), nullptr);
	EXPECT_EQ(slowEffect(variantImmuneEnemy), nullptr);
	EXPECT_EQ(slowEffect(spellLockedEnemy), nullptr)
		<< "Spell Lock is excluded by the ordinary magical-effect target policy";
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed)
		<< "The distinct spell does not consume the legacy Temporal Field toggle budget";
}

TEST_F(NewHorizonsMassSlowTest, DistinctMassSlowCanBeCastAgainNextRoundWithoutUsingTheLegacyBudget)
{
	prepareHero(true, MasteryLevel::ADVANCED, 100);
	prepareBattle();
	const auto variant = massSlowSpell();
	const auto * definition = variant.toSpell();
	ASSERT_NE(definition, nullptr);
	const int manaCost = battle()->battleGetSpellCost(definition, attackerSideHero);
	const int manaBefore = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(castSpell(variant));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - manaCost);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
	EXPECT_EQ(slowEffectCount(firstEnemy), 1u);

	ASSERT_NO_FATAL_FAILURE(advanceRoundForAttackerAction())
		<< "One explicit BattleNextRound keeps this continuation bounded";
	ASSERT_TRUE(castSpell(variant));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 2 * manaCost);
	EXPECT_EQ(slowEffectCount(firstEnemy), 1u);
	EXPECT_EQ(slowEffect(firstEnemy)->turnsRemain, 3);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(NewHorizonsMassSlowTest, OrdinarySlowRefreshAfterMassRestoresFullSpecializedMagnitudeInDetachedAndLiveState)
{
	prepareHero(true, MasteryLevel::EXPERT, 300);
	prepareBattle();
	addSlowSpecialty(-10);
	const auto variant = massSlowSpell();
	const auto * baseDefinition = SpellID(SpellID::SLOW).toSpell();
	ASSERT_NE(baseDefinition, nullptr);

	const int manaBeforeMass = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castSpell(variant));
	ASSERT_NE(slowEffect(firstEnemy), nullptr);
	EXPECT_EQ(slowEffect(firstEnemy)->val, -36);
	EXPECT_EQ(slowEffectCount(firstEnemy), 1u);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeMass - 9);

	ASSERT_NO_FATAL_FAILURE(advanceRoundForAttackerAction());

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	MassSlowPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto * projectedFirst = projected.battleGetUnitByID(firstEnemy->unitId());
	const auto * projectedSecond = projected.battleGetUnitByID(secondEnemy->unitId());
	ASSERT_NE(slowEffect(projectedFirst), nullptr);
	ASSERT_NE(slowEffect(projectedSecond), nullptr);
	EXPECT_EQ(slowEffect(projectedFirst)->val, -36)
		<< "The detached battle starts from the already-materialized Mass Slow value";
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, baseDefinition);
	const auto projectedMechanics = baseDefinition->battleMechanics(&projectedCast);
	EXPECT_FALSE(projectedMechanics->isMassive());
	spells::Target projectedAim{spells::Destination(projectedFirst)};
	projectedMechanics->castEval(projected.getServerCallback(), projectedAim);

	const auto * projectedFirstAfterCast = projected.battleGetUnitByID(firstEnemy->unitId());
	const auto * projectedSecondAfterCast = projected.battleGetUnitByID(secondEnemy->unitId());
	ASSERT_NE(projectedFirstAfterCast, nullptr);
	ASSERT_NE(projectedSecondAfterCast, nullptr);
	ASSERT_EQ(slowEffectCount(projectedFirstAfterCast), 1u)
		<< "Ordinary Slow refresh removes the old family member before adding its full-strength value";
	EXPECT_EQ(slowEffect(projectedFirstAfterCast)->val, -60)
		<< "Detached ordinary Slow restores the capped 50% plus the -10 specialty";
	EXPECT_EQ(slowEffect(projectedFirstAfterCast)->turnsRemain, 3);
	EXPECT_EQ(slowEffect(projectedSecondAfterCast)->val, -36)
		<< "The single-target refresh leaves Mass Slow on the other enemy unchanged";

	const int manaBeforeOrdinary = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castSpell(SpellID::SLOW, firstEnemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeOrdinary - 3);
	ASSERT_EQ(slowEffectCount(firstEnemy), 1u);
	EXPECT_EQ(slowEffect(firstEnemy)->val, -60);
	EXPECT_EQ(slowEffect(firstEnemy)->turnsRemain, 3);
	ASSERT_NE(slowEffect(secondEnemy), nullptr);
	EXPECT_EQ(slowEffect(secondEnemy)->val, -36)
		<< "The accepted ordinary Slow updates only its selected enemy";
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(NewHorizonsMassSlowTest, LegacyToggleIsRejectedAtomicallyWhenTheSavedProfileHasDistinctMassSlow)
{
	prepareHero(true);
	prepareBattle();
	const int manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_TRUE(newHorizonsMagic::hasDistinctMassSlow(battle()->getMagicRules()));
	EXPECT_FALSE(castLegacySlowToggle());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(slowEffect(friendly), nullptr);
	EXPECT_EQ(slowEffect(firstEnemy), nullptr);
	EXPECT_EQ(slowEffect(secondEnemy), nullptr);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(NewHorizonsMassSlowTest, OlderV3SnapshotWithoutTheVariantRowRetainsTheOncePerCombatToggle)
{
	oldV3Snapshot = true;
	prepareHero(true);
	prepareBattle();
	const auto variant = massSlowSpell();
	const auto & rules = battle()->getMagicRules();
	ASSERT_EQ(rules["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	EXPECT_FALSE(rules["spells"].Struct().contains(massSlowKey));
	EXPECT_FALSE(newHorizonsMagic::hasDistinctMassSlow(rules));
	EXPECT_EQ(newHorizonsMagic::spellVariantBase(rules, variant), variant);
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(rules, variant));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(rules, attackerSideHero, variant));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
	EXPECT_TRUE(attackerSideHero->hasActivePerk(sorcerySkillKey, temporalFieldPerkKey));

	const int manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castLegacySlowToggle());
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
	ASSERT_NE(slowEffect(firstEnemy), nullptr);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 9);

	ASSERT_NO_FATAL_FAILURE(advanceRoundForAttackerAction());
	const int manaAfterFirstCast = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castLegacySlowToggle());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaAfterFirstCast);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
	EXPECT_EQ(slowEffectCount(firstEnemy), 1u);
}
