/*
 * NewHorizonsReanimateTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

namespace
{
SpellID reanimateSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsMagic::SHADOW_REANIMATE_SPELL)));
}

class NewHorizonsReanimateTest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(reanimateSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int32_t spellPower = 0, const std::string & creature = "core:pikeman", int32_t count = 100)
	{
		startGame();
		const auto spell = reanimateSpell();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		const auto shadowMagic = SecondarySkill::decode(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL));
		ASSERT_GE(shadowMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(shadowMagic), MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName(creature), count));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1));
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(leftHex), count);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
	}

	void damageStack(const CStack * stack, int64_t damage, bool destroyRemains = false)
	{
		auto state = stack->acquireState();
		state->damage(damage, destroyRemains);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -damage;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	void updateUnitState(const CStack * stack, const std::function<void(battle::CUnitState &)> & updateState)
	{
		auto state = stack->acquireState();
		updateState(*state);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	bool cast(const CStack * selectedTarget)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = reanimateSpell();
		action.aimToUnit(selectedTarget);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void selectReanimator()
	{
		const std::string skill(newHorizonsMagic::SHADOW_MAGIC_SKILL);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({skill, "new-horizons:shadowMagic.malediction"});
		attackerSideHero->applyPerkSelection({skill, std::string(newHorizonsMagic::SHADOW_NIGHT_FEEDER_PERK)});
		attackerSideHero->applyPerkSelection({skill, std::string(newHorizonsMagic::SHADOW_REANIMATOR_PERK)});
	}

};
}

TEST_F(NewHorizonsReanimateTest, SavedV3IdentityAndRawSpellPowerUseOneSchoolSpellcraftFloor)
{
	prepare(1);
	const auto spell = reanimateSpell();
	const auto rules = battle()->getBattle()->getMagicRules();
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	ASSERT_TRUE(newHorizonsMagic::reanimateEnabled(rules, spell));
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, spell), 4);
	for(int mastery = 0; mastery < 4; ++mastery)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, spell, mastery), 16);

	JsonNode savedV2 = rules;
	savedV2["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	EXPECT_FALSE(newHorizonsMagic::reanimateEnabled(savedV2, spell));
	EXPECT_FALSE(newHorizonsMagic::reanimateHealingPool(savedV2, attackerSideHero, spell, 1, 0));

	JsonNode missingRow = rules;
	missingRow["spells"].Struct().erase(std::string(newHorizonsMagic::SHADOW_REANIMATE_SPELL));
	EXPECT_FALSE(newHorizonsMagic::reanimateEnabled(missingRow, spell));

	JsonNode badCost = rules;
	badCost["spells"][std::string(newHorizonsMagic::SHADOW_REANIMATE_SPELL)]["costs"].Vector()[0].Integer() = 15;
	EXPECT_FALSE(newHorizonsMagic::reanimateEnabled(badCost, spell));

	const auto shadow = SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL)));
	const auto spellcraft = SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::SPELLCRAFT_SKILL)));
	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, spell, 1, 0), 225);

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, spell), 12650);
	EXPECT_EQ(newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, spell, 1, 0), 226)
		<< "the fractional 6.325 HP term is floored only at final integral HP";

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, spell, 1, 0), 227);

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, spell, 1, 0), 229);
	EXPECT_THROW(newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, spell, -1, 0),
		std::invalid_argument);
}

TEST_F(NewHorizonsReanimateTest, ReanimatorAddsQuarterOfCasualtyPoolAfterSurvivorWounds)
{
	prepare();
	selectReanimator();
	ASSERT_TRUE(newHorizonsMagic::hasReanimatorPerk(attackerSideHero));
	const auto rules = battle()->getBattle()->getMagicRules();
	const auto spell = reanimateSpell();

	const auto noWoundPool = newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, spell, 0, 0);
	const auto woundedPool = newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, spell, 0, 5);
	const auto woundConsumesPool = newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, spell, 0, 1000);
	ASSERT_TRUE(noWoundPool);
	ASSERT_TRUE(woundedPool);
	ASSERT_TRUE(woundConsumesPool);
	EXPECT_EQ(*noWoundPool, 275) << "Reanimator adds one quarter of the 220-point casualty pool";
	EXPECT_EQ(*woundedPool, 273) << "floor((220 - 5) / 4) = 53 extra HP after survivor wounds";
	EXPECT_EQ(*woundConsumesPool, 220) << "wounds at or above the base pool leave no casualty bonus HP";

	const auto manaBefore = attackerSideHero->getManaAvailable();
	damageStack(target, target->getMaxHealth() * 40 + target->getMaxHealth() / 2);
	const auto countBefore = target->getCount();
	ASSERT_TRUE(cast(target));
	EXPECT_EQ(target->getCount() - countBefore, 27);
	EXPECT_EQ(target->health.getResurrected(), 27);
	EXPECT_EQ(target->getAvailableHealth(), 595 + 273);
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
}

TEST_F(NewHorizonsReanimateTest, CastHealsFirstSurvivorThenRestoresUsableUndeadAsTemporaryUnits)
{
	prepare(0, "core:skeleton");
	ASSERT_TRUE(target->unitType()->hasBonusOfType(BonusType::UNDEAD));
	const auto maximumHealth = target->getMaxHealth();
	damageStack(target, maximumHealth * 40 + maximumHealth / 2);
	ASSERT_EQ(target->getCount(), 60);
	ASSERT_EQ(target->getFirstHPleft(), maximumHealth / 2);
	const auto originalCount = target->getCount();

	ASSERT_TRUE(cast(target));
	EXPECT_EQ(target->getAvailableHealth(), 357 + 220);
	EXPECT_EQ(target->getCount() - originalCount, 37);
	EXPECT_EQ(target->health.getResurrected(), 37);
	EXPECT_TRUE(target->unitType()->hasBonusOfType(BonusType::UNDEAD));
	ASSERT_FALSE(server.battleLogLines.empty());
	EXPECT_NE(server.battleLogLines.back().find("Re-animate restores 220 health"), std::string::npos);

	const auto saved = target->save();
	EXPECT_EQ(saved["state"]["health"]["resurrected"].Integer(), 37);
	auto restored = target->acquireState();
	ASSERT_NO_THROW(restored->load(saved));
	EXPECT_EQ(restored->health.getResurrected(), 37);
	EXPECT_EQ(restored->getAvailableHealth(), target->getAvailableHealth());
}

TEST_F(NewHorizonsReanimateTest, DeadStackWithUsableRemainsCanBeReanimated)
{
	prepare();
	damageStack(target, target->getTotalHealth());
	ASSERT_TRUE(target->isDead());
	ASSERT_EQ(target->getUnusableRemains(), 0);

	ASSERT_TRUE(cast(target));
	EXPECT_EQ(target->getCount(), 22);
	EXPECT_EQ(target->health.getResurrected(), 22);
	EXPECT_EQ(target->getAvailableHealth(), 220);
}

TEST_F(NewHorizonsReanimateTest, RecastRestoresMoreUsableRemainsAndTemporaryCountIsRemovable)
{
	prepare();
	damageStack(target, target->getMaxHealth() * 55);
	ASSERT_EQ(target->getCount(), 45);
	ASSERT_TRUE(cast(target));
	ASSERT_EQ(target->getCount(), 67);
	ASSERT_EQ(target->health.getResurrected(), 22);

	advanceRound();
	ASSERT_TRUE(cast(target));
	ASSERT_EQ(target->getCount(), 89);
	ASSERT_EQ(target->health.getResurrected(), 44);
	EXPECT_EQ(target->save()["state"]["health"]["resurrected"].Integer(), 44);

	auto resultState = target->acquireState();
	resultState->health.takeResurrected();
	EXPECT_EQ(resultState->getCount(), 45)
		<< "the same cleanup primitive used by BattleResultProcessor discards only temporary bodies";
	EXPECT_EQ(resultState->health.getResurrected(), 0);
}

TEST_F(NewHorizonsReanimateTest, RejectsHostileFullNoRemainsCloneAndPhantomTargets)
{
	prepare();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

	auto * fullFriend = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 4), 10);
	ASSERT_NE(fullFriend, nullptr);
	EXPECT_FALSE(cast(fullFriend)) << "a full target is not eligible";

	auto * clone = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 4), 10);
	ASSERT_NE(clone, nullptr);
	updateUnitState(clone, [](battle::CUnitState & state) { state.cloned = true; });
	EXPECT_FALSE(cast(clone));

	auto * phantom = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 4), 1);
	ASSERT_NE(phantom, nullptr);
	updateUnitState(phantom, [](battle::CUnitState & state)
	{
		state.summoned = true;
		state.initializePhantomProfile(10, newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS);
	});
	EXPECT_FALSE(cast(phantom));

	damageStack(fullFriend, fullFriend->getTotalHealth(), true);
	EXPECT_EQ(fullFriend->getUnusableRemains(), fullFriend->unitBaseAmount());
	EXPECT_FALSE(cast(fullFriend)) << "Disintegrate-style destroyed casualties have no usable remains";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
}

TEST_F(NewHorizonsReanimateTest, DisintegratedCasualtiesCannotAuthorizeCastButSurvivorWoundsCanBeHealed)
{
	prepare();
	const auto maximumHealth = target->getMaxHealth();
	damageStack(target, maximumHealth * 40, true);
	ASSERT_EQ(target->getCount(), 60);
	ASSERT_EQ(target->getUnusableRemains(), 40);
	EXPECT_FALSE(cast(target)) << "unusable corpse HP is excluded for living stacks too";

	damageStack(target, 5);
	ASSERT_EQ(target->getAvailableHealth(), 595);
	ASSERT_TRUE(cast(target));
	EXPECT_EQ(target->getAvailableHealth(), 600);
	EXPECT_EQ(target->getCount(), 60);
	EXPECT_EQ(target->health.getResurrected(), 0);
	ASSERT_FALSE(server.battleLogLines.empty());
	EXPECT_NE(server.battleLogLines.back().find("Re-animate heals 5 health"), std::string::npos);
}

TEST_F(NewHorizonsReanimateTest, TemporaryShieldHpDoesNotHideCreatureWoundsFromReanimate)
{
	prepare();
	damageStack(target, target->getMaxHealth() * 40 + target->getMaxHealth() / 2);
	updateUnitState(target, [](battle::CUnitState & state) { state.health.addTemporaryHitPoints(405); });
	ASSERT_EQ(target->getAvailableHealth(), target->getTotalHealth())
		<< "available HP includes the shield, but actual creature HP is still missing 405";

	ASSERT_TRUE(cast(target));
	EXPECT_EQ(target->getCount(), 82);
	EXPECT_EQ(target->getAvailableHealth(), target->getTotalHealth() + 220);
	EXPECT_EQ(target->health.getResurrected(), 22);
}

TEST_F(NewHorizonsReanimateTest, RejectsDeadCorpseWhenAnotherUnitBlocksItsHex)
{
	prepare();
	damageStack(target, target->getTotalHealth());
	ASSERT_TRUE(target->isDead());
	auto * blocker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), target->getPosition(), 1);
	ASSERT_NE(blocker, nullptr);
	EXPECT_FALSE(cast(target));
}
