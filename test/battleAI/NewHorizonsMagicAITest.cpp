/*
 * NewHorizonsMagicAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../spells/NewHorizonsMagicProfileFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/CSpell.h"

namespace
{
class MagicEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MagicEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class MagicCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	MagicCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsMagicAITest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}
};

TEST_F(NewHorizonsMagicAITest, SacrificeAccountsForRemovedVictimAndKeepsHypotheticChangesPrivate)
{
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::SACRIFICE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")),
		3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = 1000;
	auto * victim = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(2, 5), 300);
	auto * corpse = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 5), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(14, 5), 500);
	const auto victimHealth = victim->getAvailableHealth();

	BattleUnitsChanged setup;
	setup.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit != victim && unit != corpse)
			setup.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	}
	auto deadState = corpse->acquireState();
	auto damage = corpse->getAvailableHealth();
	deadState->damage(damage);
	setup.changedStacks.emplace_back(corpse->unitId(), UnitChanges::EOperation::UPDATE);
	setup.changedStacks.back().data = deadState->save();
	setup.changedStacks.back().healthDelta = -damage;
	gameHandler->sendAndApply(setup);
	ASSERT_FALSE(corpse->alive());
	ASSERT_FALSE(corpse->isGhost());
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = victim->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	const auto * spell = SpellID(SpellID::SACRIFICE).toSpell();
	const battle::Target target{battle::Destination(corpse), battle::Destination(victim)};
	spells::BattleCast liveCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	ASSERT_TRUE(spell->battleMechanics(&liveCast)->canBeCastAt(target));
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, victim, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(victim);
	// Restoring one peasant is not worth losing the only large friendly army.
	EXPECT_FALSE(evaluator.attemptCastingSpell(victim));
	EXPECT_TRUE(callback->submitted.empty());

	HypotheticBattle model(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast simulated(&model, attackerSideHero, spells::Mode::HERO, spell);
	simulated.castEval(model.getServerCallback(), target);
	EXPECT_TRUE(model.battleGetUnitByID(corpse->unitId())->alive());
	EXPECT_TRUE(model.battleGetUnitByID(victim->unitId())->isGhost());
	EXPECT_EQ(model.battleGetUnitByID(victim->unitId())->getAvailableHealth(), 0);
	EXPECT_FALSE(corpse->alive());
	EXPECT_FALSE(victim->isGhost());
	EXPECT_EQ(victim->getAvailableHealth(), victimHealth);
	EXPECT_EQ(attackerSideHero->mana, 1000);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::SACRIFICE;
	action.setTarget(target);
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(corpse->alive());
	EXPECT_TRUE(victim->isGhost());
	EXPECT_EQ(attackerSideHero->mana, 1000 - cost);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
}

TEST_F(NewHorizonsMagicAITest, TeleportEvaluatorMovesSlowArmyToDistantThreatWithoutMutatingLivePreview)
{
	useCommands = false; // Isolate the already-authored spell, not a new command rule.
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::TELEPORT);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = 1000;

	const BattleHex origin(2, 5);
	const BattleHex distantPosition(14, 5);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:stoneGolem"), origin, 300);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(3, 5), 1);
	auto * distant = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), distantPosition, 150);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	ASSERT_LT(active->getMovementRange() * 2, BattleHex::getDistance(origin, distantPosition));
	const auto * spell = SpellID(SpellID::TELEPORT).toSpell();
	ASSERT_TRUE(spell->canBeCast(battle(), spells::Mode::HERO, attackerSideHero));
	const auto cost = attackerSideHero->getSpellCost(spell);

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, SpellID::TELEPORT);
	EXPECT_EQ(active->getPosition(), origin);
	EXPECT_EQ(distant->getPosition(), distantPosition);
	EXPECT_EQ(attackerSideHero->mana, 1000);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_NE(active->getPosition(), origin);
	EXPECT_EQ(distant->getPosition(), distantPosition);
	EXPECT_EQ(attackerSideHero->mana, 1000 - cost);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
}

TEST_F(NewHorizonsMagicAITest, RealEvaluatorUsesInstalledSavedHavocRankAndCost)
{
	// No magic-setting fixture override: actual curated module activation must
	// supply these rules at ordinary new-game initialization.
	prepareCommands(true);
	const bool managed = newHorizonsTest::managedMissileProfileRequested();
	ASSERT_EQ(gameState()->getMagicRules()["rulesetVersion"].Integer(), managed ? 2 : 1);
	ASSERT_EQ(gameState()->getMagicRules()["spells"].Struct().size(), managed ? 70u : 69u);
	ASSERT_EQ(battle()->battleGetActiveSpellSchools().size(), 6u);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,
		99 * attackerSideHero->getEffectPowerDivisor(spell), ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = 1000;
	ASSERT_EQ(spell->calculateDamage(attackerSideHero), 7725);
	SpellSchool best;
	ASSERT_EQ(attackerSideHero->getSpellSchoolLevel(spell, &best), 3);
	ASSERT_EQ(best, SpellSchool::fromSerializationKey("new-horizons:havoc"));
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_GT(spell->calculateDamage(attackerSideHero),
		battle()->calculateDmgRange(BattleAttackInfo(active, enemy, 0, false)).damage.max / 2);
	ASSERT_LT(spell->calculateDamage(attackerSideHero), enemy->getAvailableHealth());
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	ASSERT_EQ(callback->submitted.front().actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(callback->submitted.front().spell, SpellID::IMPLOSION);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->submitted.front()));
	EXPECT_EQ(attackerSideHero->mana, 1000 - cost);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
}
