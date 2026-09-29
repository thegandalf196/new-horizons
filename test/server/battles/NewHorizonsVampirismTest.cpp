/*
 * NewHorizonsVampirismTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/entities/hero/CHero.h"

namespace
{
SpellID vampirismSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsMagic::SHADOW_VAMPIRISM_SPELL)));
}

class NewHorizonsVampirismTest : public HeroCommandFixture
{
protected:
	CStack * bearer = nullptr;
	CStack * enemy = nullptr;
	CStack * secondEnemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(vampirismSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(const std::string & bearerCreature = "core:pikeman", int32_t bearerCount = 100)
	{
		startGame();
		const auto spell = vampirismSpell();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		const auto shadowMagic = SecondarySkill::decode(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL));
		ASSERT_GE(shadowMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(shadowMagic), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(SpellID::DISPEL);
		setTestSpellPointTotal(defenderSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		bearer = addStack(BattleSide::ATTACKER, creatureByName(bearerCreature), BattleHex(5, 5), bearerCount);
		if(bearerCreature == "core:hydra")
		{
			enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 1000);
			secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(5, 4), 1000);
		}
		else
		{
			enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 100);
			secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 100);
		}
		ASSERT_NE(bearer, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(secondEnemy, nullptr);
		beginCombat();
	}

	void injure(const CStack * stack, int64_t amount)
	{
		auto state = stack->acquireState();
		state->damage(amount);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -amount;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	bool cast(CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = vampirismSpell();
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castDispel(CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::DEFENDER;
		action.spell = SpellID::DISPEL;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(BattleSide::DEFENDER), action);
	}

	bool advanceTo(const CStack * target)
	{
		for(size_t step = 0; step < battle()->stacks.size() * 2 + 2; ++step)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == target->unitId())
				return true;
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	std::shared_ptr<const Bonus> status(const CStack * unit) const
	{
		if(!unit)
			return {};
		const auto trigger = ScriptID(ScriptID::decode(std::string(newHorizonsMagic::SHADOW_VAMPIRISM_STATUS)));
		const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(vampirismSpell())).And(Selector::typeSubtype(BonusType::COMBAT_EVENT_TRIGGER,
			BonusSubtypeID(trigger))));
		return bonuses->empty() ? std::shared_ptr<const Bonus>{} : bonuses->front();
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	void selectNightFeeder()
	{
		const std::string skill(newHorizonsMagic::SHADOW_MAGIC_SKILL);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({skill, "new-horizons:shadowMagic.malediction"});
		attackerSideHero->applyPerkSelection({skill,
			std::string(newHorizonsMagic::SHADOW_NIGHT_FEEDER_PERK)});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(skill,
			std::string(newHorizonsMagic::SHADOW_NIGHT_FEEDER_PERK)));
	}
};
}

TEST_F(NewHorizonsVampirismTest, SavedV3RegistersCanonicalLevelFourSpellAndOldProfilesDoNotAcquireIt)
{
	const auto spell = vampirismSpell();
	ASSERT_NE(spell, SpellID::NONE);
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_TRUE(newHorizonsMagic::vampirismEnabled(rules, spell));
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, spell), 4);
	for(int mastery = 0; mastery < 4; ++mastery)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, spell, mastery), 15);

	JsonNode savedV2 = rules;
	savedV2["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	EXPECT_FALSE(newHorizonsMagic::vampirismEnabled(savedV2, spell));
	EXPECT_FALSE(newHorizonsMagic::vampirismHealBasisPoints(savedV2, nullptr, spell, 100).has_value());

	JsonNode missingRow = rules;
	missingRow["spells"].Struct().erase(std::string(newHorizonsMagic::SHADOW_VAMPIRISM_SPELL));
	EXPECT_FALSE(newHorizonsMagic::vampirismEnabled(missingRow, spell));
	EXPECT_FALSE(newHorizonsMagic::vampirismHealBasisPoints(missingRow, nullptr, spell, 100).has_value());
}

TEST_F(NewHorizonsVampirismTest, SpellPowerSchoolSpellcraftAndCastBonusesAffectOnlyThePowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = vampirismSpell();
	const auto & rules = battle()->getBattle()->getMagicRules();
	const auto shadow = SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL)));
	const auto spellcraft = SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::SPELLCRAFT_SKILL)));

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, 0), 2500);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, 100), 4000);

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, 100), 4225);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, 20), 2879);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, 20, 20, 25), 3069);

	selectNightFeeder();
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, 0), 4000);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, 100), 6145);
	EXPECT_EQ(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, 1000), 6500)
		<< "Night Feeder adds 15 points after the ordinary base cap";
	EXPECT_THROW(newHorizonsMagic::vampirismHealBasisPoints(rules, attackerSideHero, spell, -1),
		std::invalid_argument);
}

TEST_F(NewHorizonsVampirismTest, CastAddsOneThreeRoundStatusAndInvalidEnemyTargetSpendsNothing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto actionsBefore = server.startedActions.size();
	const auto enemyHealthBefore = enemy->getAvailableHealth();
	EXPECT_FALSE(cast(enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(server.startedActions.size(), actionsBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
	EXPECT_EQ(status(enemy), nullptr);

	ASSERT_TRUE(cast(bearer));
	const auto applied = status(bearer);
	ASSERT_NE(applied, nullptr);
	EXPECT_EQ(applied->duration, BonusDuration::N_TURNS);
	EXPECT_EQ(applied->turnsRemain, newHorizonsMagic::VAMPIRISM_BASE_DURATION_ROUNDS);
	EXPECT_EQ(applied->val, 4675);
	EXPECT_EQ(status(enemy), nullptr);
}

TEST_F(NewHorizonsVampirismTest, NightFeederAddsFifteenPointsAfterTheOrdinaryCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	selectNightFeeder();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(cast(bearer));
	ASSERT_NE(status(bearer), nullptr);
	EXPECT_EQ(status(bearer)->val, newHorizonsMagic::VAMPIRISM_MAX_HEAL_BASIS_POINTS);
}

TEST_F(NewHorizonsVampirismTest, RecastReplacesStatusAndRefreshesThreeRoundDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(bearer));
	ASSERT_NE(status(bearer), nullptr);
	while(battle()->getRound() == 0)
		advanceRound();
	advanceRound();
	ASSERT_NE(status(bearer), nullptr);
	EXPECT_EQ(status(bearer)->turnsRemain, 2);

	ASSERT_TRUE(cast(bearer));
	const auto statuses = bearer->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(vampirismSpell())).And(Selector::typeSubtype(BonusType::COMBAT_EVENT_TRIGGER,
		BonusSubtypeID(ScriptID(ScriptID::decode(std::string(newHorizonsMagic::SHADOW_VAMPIRISM_STATUS)))))));
	ASSERT_NE(statuses, nullptr);
	EXPECT_EQ(statuses->size(), 1u);
	ASSERT_NE(status(bearer), nullptr);
	EXPECT_EQ(status(bearer)->turnsRemain, 3);
}

TEST_F(NewHorizonsVampirismTest, OrdinaryAttackHealsFromActualDamageAndLogsTheAmount)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:devil", 10));
	ASSERT_TRUE(cast(bearer));
	injure(bearer, 150);
	const int64_t bearerBefore = bearer->getAvailableHealth();
	const int64_t enemyBefore = enemy->getAvailableHealth();
	forceMaximumDamage(bearer);
	blockRetaliation(enemy);
	ASSERT_TRUE(advanceTo(bearer));
	ASSERT_TRUE(attack(bearer, enemy->getPosition()));

	const int64_t actualDamage = enemyBefore - enemy->getAvailableHealth();
	ASSERT_GT(actualDamage, 0);
	const auto vampirism = status(bearer);
	ASSERT_NE(vampirism, nullptr);
	const int64_t expectedHeal = std::min<int64_t>(actualDamage * vampirism->val / 10000,
		bearer->getTotalHealth() - bearerBefore);
	EXPECT_EQ(bearer->getAvailableHealth() - bearerBefore, expectedHeal);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [expectedHeal](const std::string & line)
	{
		return line.find("Vampirism restores " + std::to_string(expectedHeal) + " health") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
}

TEST_F(NewHorizonsVampirismTest, RetaliationDamageAlsoHealsTheEnchantedStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:devil", 10));
	ASSERT_TRUE(cast(bearer));
	injure(bearer, 150);
	const int64_t bearerBefore = bearer->getAvailableHealth();
	const int64_t enemyBefore = enemy->getAvailableHealth();
	ASSERT_TRUE(advanceTo(enemy));
	ASSERT_TRUE(attack(enemy, bearer->getPosition()));

	int64_t damageToBearer = 0;
	for(const auto & attack : server.attacks)
		if(!attack.counter())
			for(const auto & hit : attack.bsa)
				if(hit.stackAttacked == bearer->unitId())
					damageToBearer += hit.damageAmount;
	const int64_t retaliationDamage = enemyBefore - enemy->getAvailableHealth();
	ASSERT_GT(retaliationDamage, 0);
	const auto vampirism = status(bearer);
	ASSERT_NE(vampirism, nullptr);
	const int64_t afterIncomingHit = bearerBefore - damageToBearer;
	ASSERT_GT(afterIncomingHit, 0);
	const int64_t survivingCount = (afterIncomingHit + bearer->getMaxHealth() - 1) / bearer->getMaxHealth();
	const int64_t survivingWounds = survivingCount * bearer->getMaxHealth() - afterIncomingHit;
	const int64_t expectedHeal = std::min<int64_t>(retaliationDamage * vampirism->val / 10000,
		survivingWounds);
	EXPECT_EQ(bearer->getAvailableHealth(), bearerBefore - damageToBearer + expectedHeal);
}

TEST_F(NewHorizonsVampirismTest, MultiTargetAttackUsesActualDamageFromEveryVictim)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:hydra", 10));
	ASSERT_TRUE(cast(bearer));
	injure(bearer, 300);
	const int64_t bearerBefore = bearer->getAvailableHealth();
	const int64_t firstBefore = enemy->getAvailableHealth();
	const int64_t secondBefore = secondEnemy->getAvailableHealth();
	blockRetaliation(enemy);
	blockRetaliation(secondEnemy);
	forceMaximumDamage(bearer);
	ASSERT_TRUE(advanceTo(bearer));
	ASSERT_TRUE(attack(bearer, enemy->getPosition()));

	const int64_t firstDamage = firstBefore - enemy->getAvailableHealth();
	const int64_t secondDamage = secondBefore - secondEnemy->getAvailableHealth();
	ASSERT_GT(firstDamage, 0);
	ASSERT_GT(secondDamage, 0);
	const auto vampirism = status(bearer);
	ASSERT_NE(vampirism, nullptr);
	const int64_t survivorTopHealth = bearerBefore
		- static_cast<int64_t>(bearer->getCount() - 1) * bearer->getMaxHealth();
	const int64_t expectedHeal = std::min<int64_t>(
		(firstDamage + secondDamage) * vampirism->val / 10000,
		bearer->getMaxHealth() - survivorTopHealth);
	EXPECT_EQ(bearer->getAvailableHealth() - bearerBefore, expectedHeal);
}

TEST_F(NewHorizonsVampirismTest, LivingUndeadCanReceiveTheEnchantment)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:skeleton", 100));
	ASSERT_TRUE(cast(bearer));
	EXPECT_NE(status(bearer), nullptr);
}

TEST_F(NewHorizonsVampirismTest, LivingConstructCanReceiveTheEnchantment)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:stoneGargoyle", 10));
	ASSERT_TRUE(cast(bearer));
	EXPECT_NE(status(bearer), nullptr);
}

TEST_F(NewHorizonsVampirismTest, HealingRepairsSurvivorsWithoutRevivingCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:devil", 10));
	ASSERT_TRUE(cast(bearer));
	injure(bearer, bearer->getMaxHealth() + 20);
	ASSERT_GT(bearer->getCount(), 0);
	ASSERT_LT(bearer->getCount(), 100);
	const int32_t survivorCount = bearer->getCount();
	const int64_t survivorHealthBefore = bearer->getAvailableHealth();
	blockRetaliation(enemy);
	forceMaximumDamage(bearer);
	ASSERT_TRUE(advanceTo(bearer));
	ASSERT_TRUE(attack(bearer, enemy->getPosition()));
	EXPECT_EQ(bearer->getCount(), survivorCount);
	EXPECT_GT(bearer->getAvailableHealth(), survivorHealthBefore);
}

TEST_F(NewHorizonsVampirismTest, OrdinaryDispelRemovesTheTimedStatus)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(bearer));
	ASSERT_NE(status(bearer), nullptr);
	ASSERT_TRUE(advanceTo(enemy));
	ASSERT_TRUE(castDispel(bearer));
	EXPECT_EQ(status(bearer), nullptr);
}
