/*
 * NewHorizonsSanctuaryTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

class NewHorizonsSanctuaryTest : public BattleTestFixture
{
protected:
	CStack * sanctified = nullptr;
	CStack * enemy = nullptr;
	CStack * enemyShooter = nullptr;
	CStack * second = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(bool heroesHaveCombatSpells = false)
	{
		startGame();
		if(heroesHaveCombatSpells)
		{
			giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			defenderSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
			defenderSideHero->addSpellToSpellbook(SpellID::CURSE);
			defenderSideHero->addSpellToSpellbook(SpellID::FIREBALL);
			setTestSpellPointTotal(defenderSideHero, 1000);
		}
		startBattle();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		sanctified = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(4, 5), 20);
		enemyShooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(2, 7), 20);
		second = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(10, 7), 10);
		beginCombat();
		ASSERT_NE(sanctified, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(enemyShooter, nullptr);
		ASSERT_NE(second, nullptr);
	}

	void addSanctuary(CStack * stack)
	{
		Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::SANCTIFIED,
			BonusSource::SPELL_EFFECT, 1,
			BonusSourceID(SpellID(SpellID::decode("new-horizons:sanctuary"))));
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{bonus});
		gameHandler->sendAndApply(effect);
	}

	bool submit(const battle::Unit * stack, const BattleAction & action)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), action);
	}

	BattleAction heroSpell(BattleSide side, SpellID spell, const CStack * target) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		if(spell == SpellID(SpellID::FIREBALL))
			action.aimToHex(target->getPosition());
		else
			action.aimToUnit(target);
		return action;
	}

	bool hasSanctuary(const CStack * stack) const
	{
		return stack->hasBonusOfType(BonusType::SANCTIFIED);
	}
};

TEST_F(NewHorizonsSanctuaryTest, LoadedSanctuaryCastAppliesVisibleSpellSourcedMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID sanctuary(SpellID::decode("new-horizons:sanctuary"));
	ASSERT_NE(sanctuary, SpellID::NONE);
	ASSERT_NE(sanctuary.toSpell(), nullptr);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(sanctuary);
	setTestSpellPointTotal(attackerSideHero, 100);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(submit(sanctified, heroSpell(BattleSide::ATTACKER, sanctuary, second)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 5);
	EXPECT_TRUE(hasSanctuary(second));
	const auto status = second->getBonuses(Selector::type()(BonusType::SANCTIFIED));
	ASSERT_EQ(status->size(), 1u);
	EXPECT_EQ(status->front()->duration, BonusDuration::ONE_BATTLE);
	EXPECT_EQ(status->front()->source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(status->front()->sid.as<SpellID>(), sanctuary);
	EXPECT_TRUE(vstd::contains(second->activeSpells(), sanctuary));
	EXPECT_EQ(server.castsOf(sanctuary).size(), 1u);
}

TEST_F(NewHorizonsSanctuaryTest, DirectCreatureAttacksAreRejectedBeforeMovement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(battle()->battleCanAttackUnit(enemy, sanctified));
	ASSERT_TRUE(battle()->battleCanShoot(enemyShooter, sanctified->getPosition()));
	addSanctuary(sanctified);

	ASSERT_TRUE(battle()->battleMatchOwner(enemy, sanctified))
		<< "battleMatchOwner is true for opposing sides (sameOwner defaults to false)";
	EXPECT_FALSE(battle()->battleCanAttackUnit(enemy, sanctified));
	EXPECT_FALSE(battle()->battleCanShoot(enemyShooter, sanctified->getPosition()));

	const auto originalPosition = enemy->getPosition();
	auto projected = enemy->acquireState();
	projected->setPosition(BattleHex(5, 5));
	EXPECT_TRUE(battle()->isMeleeAttackPossible(projected.get(), sanctified))
		<< "the shared melee geometry query stays usable for secondary/indirect mechanics";

	const auto startedActionsBefore = server.startedActions.size();
	const auto melee = BattleAction::makeMeleeAttack(enemy, sanctified, BattleHex(5, 5), false);
	EXPECT_FALSE(submit(enemy, melee));
	EXPECT_EQ(enemy->getPosition(), originalPosition)
		<< "the illegal target must be rejected before the attacker approaches it";
	EXPECT_EQ(server.startedActions.size(), startedActionsBefore);
	EXPECT_TRUE(hasSanctuary(sanctified));

	const auto shot = BattleAction::makeShotAttack(enemyShooter, sanctified);
	EXPECT_FALSE(submit(enemyShooter, shot));
	EXPECT_TRUE(hasSanctuary(sanctified));
}

TEST_F(NewHorizonsSanctuaryTest, HeroAndCreatureHostileSingleTargetSpellsCannotSelectSanctifiedStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	addSanctuary(sanctified);

	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast cast(battle(), defenderSideHero, spells::Mode::HERO, arrow);
	spells::Target target{battle::Destination(sanctified)};
	spells::detail::ProblemImpl problem;
	const auto mechanics = arrow->battleMechanics(&cast);
	EXPECT_FALSE(mechanics->canBeCastAt(target, problem));
	const auto * curse = SpellID(SpellID::CURSE).toSpell();
	ASSERT_NE(curse, nullptr);
	spells::BattleCast curseCast(battle(), defenderSideHero, spells::Mode::HERO, curse);
	spells::detail::ProblemImpl curseProblem;
	EXPECT_FALSE(curse->battleMechanics(&curseCast)->canBeCastAt(target, curseProblem))
		<< "A negative nondamage spell is hostile even without VCMI's offensive flag";

	const auto manaBefore = defenderSideHero->getManaAvailable();
	const auto startedActionsBefore = server.startedActions.size();
	EXPECT_FALSE(submit(enemy, heroSpell(BattleSide::DEFENDER, SpellID::MAGIC_ARROW, sanctified)));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(server.startedActions.size(), startedActionsBefore);
	EXPECT_TRUE(hasSanctuary(sanctified));
	EXPECT_FALSE(submit(enemy, heroSpell(BattleSide::DEFENDER, SpellID::CURSE, sanctified)));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), manaBefore);

	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	const auto healthBefore = sanctified->getAvailableHealth();
	const auto creatureCast = BattleAction::makeCreatureSpellcast(enemy, target, SpellID::MAGIC_ARROW);
	EXPECT_FALSE(submit(enemy, creatureCast));
	EXPECT_EQ(sanctified->getAvailableHealth(), healthBefore);
	EXPECT_TRUE(hasSanctuary(sanctified));
}

TEST_F(NewHorizonsSanctuaryTest, AreaSpellCanStillHitSanctifiedStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	addSanctuary(sanctified);
	const auto healthBefore = sanctified->getAvailableHealth();
	const auto * mechanicsSpell = SpellID(SpellID::FIREBALL).toSpell();
	spells::BattleCast cast(battle(), defenderSideHero, spells::Mode::HERO, mechanicsSpell);
	spells::Target target{battle::Destination(sanctified->getPosition())};
	spells::detail::ProblemImpl problem;
	const auto mechanics = mechanicsSpell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target, problem));

	ASSERT_TRUE(submit(enemy, heroSpell(BattleSide::DEFENDER, SpellID::FIREBALL, sanctified)));
	EXPECT_LT(sanctified->getAvailableHealth(), healthBefore);
	EXPECT_TRUE(hasSanctuary(sanctified));
}

TEST_F(NewHorizonsSanctuaryTest, WaitAndDefendPreserveSanctuary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addSanctuary(sanctified);
	ASSERT_TRUE(submit(sanctified, BattleAction::makeWait(sanctified)));
	EXPECT_TRUE(hasSanctuary(sanctified));

	addSanctuary(second);
	ASSERT_TRUE(submit(second, BattleAction::makeDefend(second)));
	EXPECT_TRUE(hasSanctuary(second));
}

TEST_F(NewHorizonsSanctuaryTest, AcceptedMovementAndOffensiveCreatureCastBreakSanctuary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addSanctuary(sanctified);
	ASSERT_TRUE(submit(sanctified, BattleAction::makeMove(sanctified, BattleHex(7, 5))));
	EXPECT_NE(sanctified->getPosition(), BattleHex(6, 5));
	EXPECT_FALSE(hasSanctuary(sanctified));

	// A separate marked caster verifies that the marker is gone before its
	// offensive active ability resolves.
	addSanctuary(enemy);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	const auto healthBefore = sanctified->getAvailableHealth();
	spells::Target target{battle::Destination(sanctified)};
	ASSERT_TRUE(submit(enemy, BattleAction::makeCreatureSpellcast(enemy, target, SpellID::MAGIC_ARROW)));
	EXPECT_FALSE(hasSanctuary(enemy));
	EXPECT_LT(sanctified->getAvailableHealth(), healthBefore);
}

TEST_F(NewHorizonsSanctuaryTest, AcceptedAttackBreaksSanctuary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addSanctuary(sanctified);
	BattleStackMoved move;
	move.battleID = BattleID(0);
	move.stack = enemy->unitId();
	move.teleporting = true;
	move.tilesToMove.insert(BattleHex(5, 5));
	gameHandler->sendAndApply(move);
	BattleTestFixture::blockRetaliation(enemy);
	ASSERT_TRUE(attack(sanctified, enemy->getPosition()));
	EXPECT_FALSE(hasSanctuary(sanctified));
}
