/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../../lib/battle/NewHorizonsCommandResources.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/battles/BattleActivationEffects.h"

class StoneboundPeriodicDamageTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped New Horizons content";
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void select(CGHeroInstance * hero, const std::string & skill, const std::string & perk, int rank)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), rank, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({skill, skill + "." + perk});
	}
	void activate(CStack * unit)
	{
		BattleSetActiveStack packet;
		packet.battleID = BattleID(0); packet.stack = unit->unitId(); packet.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(packet);
	}
	void prepare()
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		select(attackerSideHero, newHorizonsElementalAttunement::SKILL_ID, "elementalConduit", 1);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:natureMagic")), 1, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode("new-horizons:poison")));
		setTestSpellPointTotal(attackerSideHero, 1000);
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:erdamon"))); // Might Conflux: both skills are legal.
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 48, ChangeValueMode::ABSOLUTE);
		select(defenderSideHero, newHorizonsElementalAttunement::SKILL_ID, "elementalAttunement", 1);
		select(defenderSideHero, newHorizonsElementalAttunement::SKILL_ID, "adaptiveElement", 2);
		select(defenderSideHero, newHorizonsElementalAttunement::SKILL_ID, "perfectConvergence", 3);
		select(defenderSideHero, "new-horizons:command", "aggressiveCommander", 1);
		select(defenderSideHero, "new-horizons:command", "crisisCommand", 2);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false)) remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 1000);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(72), 1000);
		SetStackEffect quiet; quiet.battleID = BattleID(0);
		for(const auto * unit : {actor, victim}) quiet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{
			Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID())});
		gameHandler->sendAndApply(quiet);
		beginCombat(); activate(victim);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeHeroCommand(BattleSide::DEFENDER, HeroCommand::BRACE)));
		ASSERT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 60);
		ASSERT_EQ(victim->elementalAttunementPassives().physicalReductionBasisPoints, 1500);
		ASSERT_TRUE(newHorizonsCommand::unbrokenRanks(*battle(), victim));
	}
	void applyCreaturePoison(int base)
	{
		auto state = victim->acquireState();
		ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), base, actor->unitId()));
		BattleUnitsChanged update; update.battleID = BattleID(0);
		update.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::UPDATE);
		update.changedStacks.back().data = state->save();
		gameHandler->sendAndApply(update);
		ASSERT_EQ(victim->physicalPoisonSpellOrigin(), BattleSide::NONE);
	}
	void expectTick(int64_t damage, int remaining)
	{
		const auto before = victim->getAvailableHealth();
		const auto command = battle()->getCommandResources(BattleSide::DEFENDER);
		battleActivationEffects::apply(gameHandler.get(), *battle(), victim, BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_EQ(victim->getAvailableHealth(), before - damage);
		EXPECT_EQ(victim->physicalPoisonActivationsRemaining, remaining);
		EXPECT_EQ(battle()->getCommandResources(BattleSide::DEFENDER), command);
	}
};

TEST_F(StoneboundPeriodicDamageTest, HeroPoisonUsesExistingSpellMitigationThenStoneboundAndOriginalArmyUnderControl)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(actor);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID(SpellID::decode("new-horizons:poison")), victim));
	ASSERT_EQ(victim->physicalPoisonBaseDamage, 23);
	ASSERT_EQ(victim->physicalPoisonSpellOrigin(), BattleSide::ATTACKER);
	ASSERT_NO_FATAL_FAILURE(expectTick(15, 2)); // floor(floor(23 * .8) * .85).
	SetStackEffect control; control.battleID = BattleID(0);
	control.toAdd.emplace_back(victim->unitId(), std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(control);
	ASSERT_EQ(battle()->battleGetOwner(victim), PlayerColor(0));
	EXPECT_EQ(victim->unitSide(), BattleSide::DEFENDER);
	EXPECT_EQ(victim->elementalAttunementPassives().physicalReductionBasisPoints, 1500);
	EXPECT_FALSE(newHorizonsCommand::unbrokenRanks(*battle(), victim));
	ASSERT_NO_FATAL_FAILURE(expectTick(28, 1)); // Original-army Stonebound remains; the hostile Order no longer benefits it.
	ASSERT_NO_FATAL_FAILURE(expectTick(39, 0));
	EXPECT_EQ(victim->physicalPoisonBaseDamage, 0);
}

TEST_F(StoneboundPeriodicDamageTest, CreaturePoisonGetsStoneboundWithoutBecomingAnAttackOrHeroSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(applyCreaturePoison(20));
	ASSERT_TRUE(newHorizonsCommand::unbrokenRanks(*battle(), victim));
	ASSERT_NO_FATAL_FAILURE(expectTick(17, 2)); // No extra .8: the creature DOT is neither an attack nor a hero spell.
	ASSERT_NO_FATAL_FAILURE(expectTick(25, 1));
	ASSERT_NO_FATAL_FAILURE(expectTick(34, 0));
}

TEST_F(StoneboundPeriodicDamageTest, ZeroDamageProtectedTicksStillConsumeTheirThreeActivationLifetime)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(applyCreaturePoison(1));
	ASSERT_NO_FATAL_FAILURE(expectTick(0, 2));
	ASSERT_NO_FATAL_FAILURE(expectTick(0, 1));
	ASSERT_NO_FATAL_FAILURE(expectTick(1, 0));
	EXPECT_EQ(victim->physicalPoisonBaseDamage, 0);
}
