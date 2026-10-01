/*
 * NewHorizonsGrandFormulaTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

namespace
{
constexpr auto SPELLCRAFT_SKILL = "new-horizons:spellcraft";
constexpr auto ARCANE_FOCUS_PERK = "new-horizons:spellcraft.arcaneFocus";
constexpr auto EMPOWER_SPELL_PERK = "new-horizons:spellcraft.empowerSpell";
constexpr auto GRAND_FORMULA_PERK = "new-horizons:spellcraft.grandFormula";
constexpr auto HAVOC_MAGIC_SKILL = "new-horizons:havocMagic";
constexpr auto SORCERY_MAGIC_SKILL = "new-horizons:sorceryMagic";

SpellID timeStopSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsSorcery::TIME_STOP_SPELL)));
}

class NewHorizonsGrandFormulaTest : public HeroCommandFixture
{
protected:
	bool grandFormulaPlanned = false;
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;
	CStack * creatureCaster = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(grandFormulaPlanned)
		{
			for(auto & perk : perkRules["skills"][SPELLCRAFT_SKILL]["perks"].Vector())
				if(perk["id"].String() == GRAND_FORMULA_PERK)
					perk["effect"]["status"].String() = "planned";
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void prepareSpellcraft(int32_t spellPower = 150)
	{
		startGame();
		const auto spellcraft = SecondarySkill(SecondarySkill::decode(SPELLCRAFT_SKILL));
		const auto havoc = SecondarySkill(SecondarySkill::decode(HAVOC_MAGIC_SKILL));
		const auto sorcery = SecondarySkill(SecondarySkill::decode(SORCERY_MAGIC_SKILL));
		ASSERT_TRUE(spellcraft.hasValue());
		ASSERT_TRUE(havoc.hasValue());
		ASSERT_TRUE(sorcery.hasValue());

		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, ARCANE_FOCUS_PERK});
		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, EMPOWER_SPELL_PERK});
		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::CHAIN_LIGHTNING));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::ARMAGEDDON));
		attackerSideHero->addSpellToSpellbook(timeStopSpell());
		setTestSpellPointTotal(attackerSideHero, 1000);
	}

	void prepareDefenderCounterspellWard()
	{
		// The accepted cast records the level even when a real opposing ward
		// negates it. Counterspell only suppresses an enemy spell while its
		// defending hero can pay the resolved counter-cost.
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(SpellID(SpellID::HASTE));
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(defenderSideHero, 1000);
		ASSERT_GE(defenderSideHero->getManaAvailable(), 100);
	}

	void startFormulaBattle(bool withCreatureCaster = false)
	{
		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!removed.changedStacks.empty())
			gameHandler->sendAndApply(removed);

		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		if(withCreatureCaster)
		{
			creatureCaster = addStack(BattleSide::ATTACKER, creatureByName("core:imp"), BattleHex(leftHex + 3), 1);
			ASSERT_NE(creatureCaster, nullptr);
		}
		beginCombat();
		activateStack(friendly);
	}

	void activateStack(const CStack * stack)
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = stack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	std::unique_ptr<spells::Mechanics> mechanics(SpellID spell,
		const CBattleInfoCallback * battleCallback = nullptr) const
	{
		const auto * definition = spell.toSpell();
		if(!definition)
			return nullptr;
		spells::BattleCast cast(battleCallback ? battleCallback : battle(), attackerSideHero,
			spells::Mode::HERO, definition);
		return definition->battleMechanics(&cast);
	}

	bool issueHeroSpell(SpellID spell, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(BattleSide::ATTACKER), action);
	}

	bool issueTimeStop(const BattleHex & center)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = timeStopSpell();
		action.aimToHex(center);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(BattleSide::ATTACKER), action);
	}

	void addCreatureMagicArrowCaster(CStack * caster)
	{
		ASSERT_NE(caster, nullptr);
		caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(),
			BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
		caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
		caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 20, BonusSourceID()));
	}
};
}

TEST_F(NewHorizonsGrandFormulaTest, FirstLevelFourFormulaStacksWithArcaneFocusAndUsesExistingCastHistory)
{
	prepareSpellcraft();
	attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, GRAND_FORMULA_PERK});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(SPELLCRAFT_SKILL, GRAND_FORMULA_PERK));
	prepareDefenderCounterspellWard();
	startFormulaBattle(true);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	// Rejected hero commands and an accepted creature-mode cast do not take either
	// Spellcraft snapshot away from the next accepted hero cast.
	EXPECT_FALSE(issueHeroSpell(SpellID(SpellID::CHAIN_LIGHTNING), friendly));
	addCreatureMagicArrowCaster(creatureCaster);
	const auto * magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(magicArrow, nullptr);
	spells::BattleCast creatureCast(battle(), creatureCaster, spells::Mode::CREATURE_ACTIVE, magicArrow);
	const auto creatureMechanics = magicArrow->battleMechanics(&creatureCast);
	ASSERT_NE(creatureMechanics, nullptr);
	const spells::Target creatureTarget{spells::Destination(enemy)};
	ASSERT_TRUE(creatureMechanics->canBeCastAt(creatureTarget));
	creatureCast.cast(gameHandler->spellEnv.get(), creatureTarget);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));

	const auto * chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	ASSERT_NE(chainLightning, nullptr);
	ASSERT_EQ(battle()->battleGetSpellLevel(chainLightning->getId()), 4);
	auto firstHighMechanics = mechanics(SpellID(SpellID::CHAIN_LIGHTNING));
	ASSERT_NE(firstHighMechanics, nullptr);
	EXPECT_EQ(firstHighMechanics->getEffectPower(), 150);
	EXPECT_EQ(firstHighMechanics->getArcaneFocusBonusPercent(),
		newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS_BONUS_PERCENT);
	EXPECT_EQ(firstHighMechanics->getCastSpellPowerComponentBonusPercent(), 80)
		<< "Arcane Focus's 120% and Grand Formula's 150% compose multiplicatively";
	const int normalCoefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, chainLightning->getId());
	ASSERT_EQ(normalCoefficient, 18'850)
		<< "Expert Havoc's 145% and Expert Spellcraft's 130% are composed before cast modifiers";
	EXPECT_EQ(firstHighMechanics->getSpellPowerCoefficientBasisPoints(), normalCoefficient * 180 / 100);
	EXPECT_EQ(firstHighMechanics->getEmpowerSpellBonusPercent(),
		newHorizonsMagic::SPELLCRAFT_EMPOWER_BONUS_PERCENT);
	const auto damageFormula = newHorizonsMagic::spellDirectDamage(
		battle()->getMagicRules(), chainLightning->getJsonKey());
	ASSERT_TRUE(damageFormula.has_value());
	const auto formulaDamage = damageFormula->evaluateBasisPoints(firstHighMechanics->getEffectPower(),
		firstHighMechanics->getEffectPowerDivisor(), firstHighMechanics->getSpellPowerCoefficientBasisPoints(),
		firstHighMechanics->getEmpowerSpellBonusPercent());
	EXPECT_EQ(firstHighMechanics->getEffectValue(), formulaDamage)
		<< "The 150 raw Spell Power term is scaled before School × Spellcraft × Focus/Formula and Empower";
	EXPECT_EQ(damageFormula->evaluateBasisPoints(0, firstHighMechanics->getEffectPowerDivisor(),
		firstHighMechanics->getSpellPowerCoefficientBasisPoints(),
		firstHighMechanics->getEmpowerSpellBonusPercent()), damageFormula->base)
		<< "Neither multiplicative Spellcraft perk scales the fixed damage base";

	// The Level 1 cast spends Arcane Focus, but it leaves the shared Level 4/5
	// Grand Formula gate open for this later, still-first high-level spell.
	ASSERT_TRUE(issueHeroSpell(SpellID(SpellID::MAGIC_ARROW), enemy));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	auto afterLowCast = mechanics(SpellID(SpellID::CHAIN_LIGHTNING));
	ASSERT_NE(afterLowCast, nullptr);
	EXPECT_EQ(afterLowCast->getArcaneFocusBonusPercent(), 0);
	EXPECT_EQ(afterLowCast->getCastSpellPowerComponentBonusPercent(), 50)
		<< "A Level 1 hero cast does not consume Grand Formula's Level 4/5 allowance";

	endRound();
	activateStack(friendly);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto castCost = battle()->battleGetSpellCost(chainLightning, attackerSideHero);
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	ASSERT_TRUE(issueHeroSpell(SpellID(SpellID::CHAIN_LIGHTNING), enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - castCost);
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	const auto casts = server.castsOf(SpellID(SpellID::CHAIN_LIGHTNING));
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.counterspellNegated);

	auto afterHighCast = mechanics(timeStopSpell());
	ASSERT_NE(afterHighCast, nullptr);
	EXPECT_EQ(afterHighCast->getCastSpellPowerComponentBonusPercent(), 0)
		<< "An accepted, counterspelled Level 4 cast consumes the shared Level 4/5 gate";
	endRound();
	afterHighCast = mechanics(timeStopSpell());
	ASSERT_NE(afterHighCast, nullptr);
	EXPECT_EQ(afterHighCast->getCastSpellPowerComponentBonusPercent(), 0)
		<< "Round rollover does not renew Grand Formula";

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(restored->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(restored->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	spells::BattleCast restoredCast(restored.get(), attackerSideHero, spells::Mode::HERO,
		timeStopSpell().toSpell());
	const auto restoredMechanics = timeStopSpell().toSpell()->battleMechanics(&restoredCast);
	ASSERT_NE(restoredMechanics, nullptr);
	EXPECT_EQ(restoredMechanics->getCastSpellPowerComponentBonusPercent(), 0)
		<< "A restored battle reuses the serialized completed-level mask; no separate Formula counter is needed";
}

TEST_F(NewHorizonsGrandFormulaTest, FirstLevelFiveFormulaConsumesTheSameLevelFourOrFiveGate)
{
	prepareSpellcraft();
	attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, GRAND_FORMULA_PERK});
	prepareDefenderCounterspellWard();
	startFormulaBattle();

	const auto levelFive = timeStopSpell();
	const auto * spell = levelFive.toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_EQ(battle()->battleGetSpellLevel(spell->getId()), 5);
	auto beforeCast = mechanics(levelFive);
	ASSERT_NE(beforeCast, nullptr);
	EXPECT_EQ(beforeCast->getCastSpellPowerComponentBonusPercent(), 80);

	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	ASSERT_TRUE(issueTimeStop(BattleHex(8, 5)));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	const auto casts = server.castsOf(levelFive);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.counterspellNegated);

	auto laterLevelFour = mechanics(SpellID(SpellID::CHAIN_LIGHTNING));
	ASSERT_NE(laterLevelFour, nullptr);
	EXPECT_EQ(laterLevelFour->getCastSpellPowerComponentBonusPercent(), 0)
		<< "The first accepted Level 5 cast consumes the same gate as Level 4";
}

TEST_F(NewHorizonsGrandFormulaTest, FormulaRequiresItsSelectedPerkAtExpertRank)
{
	prepareSpellcraft();
	startFormulaBattle();

	auto * hero = attackerSideHero;
	const auto * spell = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	ASSERT_NE(spell, nullptr);
	auto absent = mechanics(SpellID(SpellID::CHAIN_LIGHTNING));
	ASSERT_NE(absent, nullptr);
	EXPECT_EQ(absent->getCastSpellPowerComponentBonusPercent(), 20)
		<< "Expert Spellcraft without a selected Grand Formula retains Arcane Focus only";

	hero->applyPerkSelection({SPELLCRAFT_SKILL, GRAND_FORMULA_PERK});
	EXPECT_TRUE(hero->hasActivePerk(SPELLCRAFT_SKILL, GRAND_FORMULA_PERK));
	auto expert = mechanics(SpellID(SpellID::CHAIN_LIGHTNING));
	ASSERT_NE(expert, nullptr);
	EXPECT_EQ(expert->getCastSpellPowerComponentBonusPercent(), 80);

	const auto spellcraft = SecondarySkill(SecondarySkill::decode(SPELLCRAFT_SKILL));
	ASSERT_TRUE(spellcraft.hasValue());
	hero->setSecSkillLevel(spellcraft, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(hero->hasActivePerk(SPELLCRAFT_SKILL, GRAND_FORMULA_PERK));
	auto advanced = mechanics(SpellID(SpellID::CHAIN_LIGHTNING));
	ASSERT_NE(advanced, nullptr);
	EXPECT_EQ(advanced->getCastSpellPowerComponentBonusPercent(), 20)
		<< "The saved selection is dormant below its required Expert rank";
}

TEST_F(NewHorizonsGrandFormulaTest, PlannedFormulaCannotBeSelectedOrAffectMechanics)
{
	grandFormulaPlanned = true;
	prepareSpellcraft();
	EXPECT_THROW(attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, GRAND_FORMULA_PERK}), std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(SPELLCRAFT_SKILL, GRAND_FORMULA_PERK));
	startFormulaBattle();

	auto planned = mechanics(SpellID(SpellID::CHAIN_LIGHTNING));
	ASSERT_NE(planned, nullptr);
	EXPECT_EQ(planned->getCastSpellPowerComponentBonusPercent(), 20)
		<< "A planned configuration row cannot enable the production multiplier";
}

TEST_F(NewHorizonsGrandFormulaTest, TimeStopUsesTheComposedPowerTermButKeepsItsFixedAreaAndLifetime)
{
	prepareSpellcraft(0);
	attackerSideHero->applyPerkSelection({SPELLCRAFT_SKILL, GRAND_FORMULA_PERK});
	startBattle();
	BattleUnitsChanged removed;
	removed.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	if(!removed.changedStacks.empty())
		gameHandler->sendAndApply(removed);
	const BattleHex center(8, 5);
	const auto innerHex = center.copyToEast();
	const auto outerHex = innerHex.copyToEast();
	const auto beyondHex = outerHex.copyToEast();
	auto * centerStack = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), center, 10);
	auto * innerStack = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), innerHex, 10);
	auto * outerStack = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), outerHex, 10);
	auto * beyondStack = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), beyondHex, 10);
	ASSERT_NE(centerStack, nullptr);
	ASSERT_NE(innerStack, nullptr);
	ASSERT_NE(outerStack, nullptr);
	ASSERT_NE(beyondStack, nullptr);
	beginCombat();
	activateStack(innerStack);

	const auto timeStop = timeStopSpell();
	const auto * spell = timeStop.toSpell();
	ASSERT_NE(spell, nullptr);
	const spells::Target aim{spells::Destination(center)};
	auto mechanics = this->mechanics(timeStop);
	ASSERT_NE(mechanics, nullptr);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 33'930);
	auto affected = mechanics->getAffectedStacks(aim);
	EXPECT_TRUE(vstd::contains(affected, innerStack));
	EXPECT_FALSE(vstd::contains(affected, outerStack))
		<< "Grand Formula scales only Time Stop's Spell Power-derived radius term";

	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 30, ChangeValueMode::ABSOLUTE);
	mechanics = this->mechanics(timeStop);
	ASSERT_NE(mechanics, nullptr);
	affected = mechanics->getAffectedStacks(aim);
	EXPECT_TRUE(vstd::contains(affected, centerStack));
	EXPECT_TRUE(vstd::contains(affected, innerStack));
	EXPECT_TRUE(vstd::contains(affected, outerStack));
	EXPECT_FALSE(vstd::contains(affected, beyondStack))
		<< "The canonical radius cap remains two hexes despite the composed multiplier";

	ASSERT_TRUE(issueTimeStop(center));
	EXPECT_TRUE(outerStack->isTimeStopped());
	const auto outerTimeStopBonuses = outerStack->getAllBonuses(Selector::type()(BonusType::TIME_STOP));
	ASSERT_FALSE(outerTimeStopBonuses->empty());
	const auto * marker = outerTimeStopBonuses->front().get();
	ASSERT_NE(marker, nullptr);
	EXPECT_EQ(marker->val, 2);
	EXPECT_EQ(marker->duration, BonusDuration::ONE_BATTLE)
		<< "The multiplied radius does not convert Time Stop's battle-long lifetime";
}
