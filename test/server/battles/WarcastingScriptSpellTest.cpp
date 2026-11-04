/*
 * WarcastingScriptSpellTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/ObstacleHandler.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/Destination.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <tuple>

namespace
{
constexpr auto warcastingSkill = "new-horizons:warcasting";
constexpr auto phantomArmyKey = newHorizonsSorcery::PHANTOM_ARMY_SPELL;
constexpr auto transfigureMatterKey = "new-horizons:transfigureMatter";

SpellID phantomArmySpell()
{
	return SpellID(SpellID::decode(phantomArmyKey));
}

SpellID transfigureMatterSpell()
{
	return SpellID(SpellID::decode(transfigureMatterKey));
}

int findObstacleID(const std::string_view key)
{
	int result = -1;
	LIBRARY->obstacles()->forEach([&](const ObstacleInfo * obstacle, bool & stop)
	{
		if(obstacle->getJsonKey() == key)
		{
			result = obstacle->getIndex();
			stop = true;
		}
	});
	return result;
}

class WarcastingScriptSpellTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		captureLegacyPerkOfferPolicy(*loaded);
	}

	void prepare(const int32_t spellPower, const int32_t attackerCount = 10)
	{
		startGame();
		const int decodedWarcasting = SecondarySkill::decode(warcastingSkill);
		ASSERT_GE(decodedWarcasting, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decodedWarcasting), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

		const auto phantom = phantomArmySpell();
		const auto transfigure = transfigureMatterSpell();
		ASSERT_NE(phantom, SpellID::NONE);
		ASSERT_NE(transfigure, SpellID::NONE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID(SpellID::SUMMON_AIR_ELEMENTAL), SpellID(SpellID::SACRIFICE), phantom, transfigure})
			attackerSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(attackerSideHero, 1000);

		// Seed ordinary army slots before battle creation. BattleUnitsChanged ADD
		// uses the summoned-slot placeholder even when its temporary flag is false,
		// which would legitimately trigger elemental-summon exclusivity.
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), attackerCount));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 1));
		startBattle();
		for(auto * stack : battle()->battleGetAllStacks())
		{
			if(stack->unitSlot() != SlotID(0))
				continue;
			if(stack->unitSide() == BattleSide::ATTACKER)
				attacker = stack;
			else
				defender = stack;
		}
		ASSERT_NE(attacker, nullptr);
		ASSERT_NE(defender, nullptr);
		ASSERT_FALSE(attacker->isSummoned());
		beginCombat();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != attacker && unit != defender)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		activate(attacker);
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = stack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	void issueOrderThenAdvanceRound()
	{
		ASSERT_TRUE(issue(HeroCommand::CHARGE));
		advanceRound(); // A new round restores the shared ordinary Hero action.
	}

	bool castAtUnit(const SpellID spell, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castAtHex(const SpellID spell, const BattleHex & target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToHex(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	std::shared_ptr<CObstacleInstance> addPhysicalObstacle(const int uniqueID, const BattleHex & position)
	{
		auto obstacle = std::make_shared<CObstacleInstance>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = findObstacleID("core:0");
		EXPECT_GE(obstacle->ID, 0);
		obstacle->pos = position;
		obstacle->obstacleType = CObstacleInstance::USUAL;
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	void expectRosterDeniedWithoutMutation(const BattleAction & action)
	{
		const auto * spell = action.spell.toSpell();
		const auto & row = attackerSideHero->getMagicRules()["spells"][spell->getJsonKey()];
		ASSERT_FALSE(row["heroAccess"].Bool());
		ASSERT_FALSE(row["ordinaryAcquisition"].Bool());
		ASSERT_TRUE(attackerSideHero->spellbookContainsSpell(action.spell));
		ASSERT_FALSE(attackerSideHero->canCastThisSpell(spell));
		const auto bytes = [](auto & value)
		{
			CMemorySerializer wire;
			wire.oser & value;
			return wire.extractBuffer();
		};
		const auto army = [](const CGHeroInstance * hero)
		{
			std::vector<std::tuple<SlotID, CreatureID, int64_t>> result;
			for(const auto & [slot, stack] : hero->Slots())
				result.emplace_back(slot, stack->getCreatureID(), stack->getCount());
			return result;
		};
		const auto beforeBattle = bytes(*battle());
		const auto beforeRandom = bytes(*gameHandler->randomizer);
		const auto beforeArmy = army(attackerSideHero);
		const auto beforeEnemyArmy = army(defenderSideHero);
		const auto normal = attackerSideHero->getNormalSpellPoints();
		const auto buffer = attackerSideHero->getBufferSpellPoints();
		const auto enemyMana = defenderSideHero->getManaAvailable();
		const auto actions = server.startedActions.size();
		const auto casts = server.casts.size();
		EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(bytes(*battle()), beforeBattle); // Includes ledger, history, effects and every battle stack.
		EXPECT_EQ(bytes(*gameHandler->randomizer), beforeRandom);
		EXPECT_EQ(army(attackerSideHero), beforeArmy);
		EXPECT_EQ(army(defenderSideHero), beforeEnemyArmy);
		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normal);
		EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), buffer);
		EXPECT_EQ(defenderSideHero->getManaAvailable(), enemyMana);
		EXPECT_EQ(server.startedActions.size(), actions);
		EXPECT_EQ(server.casts.size(), casts);
	}

	const CStack * attacker = nullptr;
	const CStack * defender = nullptr;
};
}

TEST_F(WarcastingScriptSpellTest, CurrentRosterRejectsInscribedRetiredSummonWithoutMutation)
{
	prepare(43);
	issueOrderThenAdvanceRound();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::SUMMON_AIR_ELEMENTAL;
	action.aimToHex(BattleHex::INVALID);
	ASSERT_NO_FATAL_FAILURE(expectRosterDeniedWithoutMutation(action));
}

TEST_F(WarcastingScriptSpellTest, CurrentRosterRejectsInscribedRetiredSacrificeWithoutMutation)
{
	prepare(43);
	auto * dead = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * victim = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(72), 100);
	int64_t casualtyDamage = dead->getAvailableHealth();
	dead->damage(casualtyDamage);
	ASSERT_FALSE(dead->alive());
	const auto victimHealth = victim->getAvailableHealth();
	issueOrderThenAdvanceRound();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::SACRIFICE;
	action.setTarget({battle::Destination(dead), battle::Destination(victim)});
	ASSERT_NO_FATAL_FAILURE(expectRosterDeniedWithoutMutation(action));
	EXPECT_FALSE(dead->alive());
	EXPECT_EQ(victim->getAvailableHealth(), victimHealth);
}

TEST_F(WarcastingScriptSpellTest, PhantomArmyUsesPowerInItsIntegrityCoefficient)
{
	constexpr int32_t spellPower = 43;
	prepare(spellPower, 1000);
	const auto spell = phantomArmySpell();
	ASSERT_EQ(attackerSideHero->getEffectPower(spell.toSpell()), spellPower);
	const auto sourceHealth = attacker->getAvailableHealth();
	const int64_t basisPoints = 2000 + 15LL * spellPower;
	const auto expectedIntegrity = sourceHealth * basisPoints / 10000;
	ASSERT_EQ(sourceHealth, 10000);
	ASSERT_EQ(basisPoints, 2645);
	issueOrderThenAdvanceRound();
	ASSERT_TRUE(castAtUnit(spell, attacker));

	const auto phantoms = battle()->battleGetStacksIf([](const CStack * unit)
	{
		return unit->getPhantomInitialIntegrity() > 0;
	});
	ASSERT_EQ(phantoms.size(), 1u);
	EXPECT_EQ(phantoms.front()->getPhantomIntegrity(), expectedIntegrity);
	EXPECT_EQ(phantoms.front()->getPhantomInitialIntegrity(), expectedIntegrity);
}

TEST_F(WarcastingScriptSpellTest, TransfigureMatterScalesItsPowerPoolButNotItsFixedTerms)
{
	constexpr int32_t spellPower = 41;
	prepare(spellPower);
	const auto obstacle = addPhysicalObstacle(20, BattleHex(8, 5));
	const auto footprint = static_cast<int64_t>(obstacle->getAffectedTiles().size());
	ASSERT_GT(footprint, 1);
	const auto expectedHealth = 80 + 2LL * spellPower + 50 * footprint;
	ASSERT_EQ(expectedHealth, 162 + 50 * footprint);
	issueOrderThenAdvanceRound();
	ASSERT_TRUE(castAtHex(transfigureMatterSpell(), obstacle->pos));

	const auto golems = battle()->battleGetStacksIf([](const CStack * unit)
	{
		return unit->unitType() && unit->unitType()->getJsonKey() == "core:diamondGolem";
	});
	ASSERT_EQ(golems.size(), 1u);
	EXPECT_EQ(golems.front()->getAvailableHealth(), expectedHealth);
	EXPECT_TRUE(battle()->obstacles.empty());
}
