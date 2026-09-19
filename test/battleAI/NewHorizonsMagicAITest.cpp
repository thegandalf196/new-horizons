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
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"

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

TEST_F(NewHorizonsMagicAITest, HeroSpellCreditsDamageToValuableEnemySummonWithoutFollowUpAttacks)
{
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(startGame());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto initialSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto id : initialSpells)
		attackerSideHero->removeSpellFromSpellbook(id);
	attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,
		2 * attackerSideHero->getEffectPowerDivisor(spell), ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = 1000;
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * valuable = addStack(BattleSide::DEFENDER, creatureByName("core:airElemental"), BattleHex(12, 5), 100);
	auto * weak = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 2), 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != valuable && unit != weak)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -active->getMovementRange();
	active->addNewBonus(std::make_shared<Bonus>(immobilized));
	ASSERT_EQ(active->getMovementRange(), 0);
	ASSERT_FALSE(active->canShoot());
	valuable->summoned = true; // Fixture state of an enemy summoned elemental.
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	const auto health = valuable->getAvailableHealth();
	ASSERT_LT(spell->calculateDamage(attackerSideHero), health);
	ASSERT_GT(spell->calculateDamage(attackerSideHero), weak->getAvailableHealth());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto baseline = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache originalCache;
	originalCache.buildDamageCache(baseline, BattleSide::ATTACKER);
	std::array<float, 2> reductions{};
	const std::array<const CStack *, 2> victims{weak, valuable};
	for(size_t index = 0; index < victims.size(); ++index)
	{
		auto model = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		spells::BattleCast cast(model.get(), attackerSideHero, spells::Mode::HERO, spell);
		const spells::Target target{spells::Destination(victims[index])};
		ASSERT_TRUE(spell->battleMechanics(&cast)->canBeCastAt(target));
		cast.castEval(model->getServerCallback(), target);
		EXPECT_FALSE(model->battleGetUnitByID(victims[index]->unitId())->isGhost());
		EXPECT_EQ(model->getForUpdate(victims[index]->unitId())->summoned, index == 1);
		DamageCache copy = originalCache;
		DamageCache cache(&copy);
		cache.buildDamageCache(model, BattleSide::ATTACKER);
		const auto * projectedActive = model->battleGetUnitByID(active->unitId());
		ASSERT_EQ(projectedActive->getMovementRange(), 0);
		PotentialTargets targets(projectedActive, cache, model);
		ASSERT_TRUE(targets.possibleAttacks.empty());
		BattleExchangeEvaluator exchange(model, environment, 1.0f, 2);
		EXPECT_EQ(exchange.findMoveTowardsUnreachable(projectedActive, targets, cache, model).score,
			EvaluationResult::INEFFECTIVE_SCORE);
		const auto lost = victims[index]->getAvailableHealth()
			- model->battleGetUnitByID(victims[index]->unitId())->getAvailableHealth();
		ASSERT_GT(lost, 0);
		reductions[index] = AttackPossibility::calculateDamageReduce(nullptr, victims[index], lost, cache, model);
	}
	ASSERT_GT(reductions[1], reductions[0]);
	ASSERT_GT(reductions[0], 0);
	for(const bool summoned : {true, false})
	{
		valuable->summoned = summoned;
		callback->submitted.clear();
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		ASSERT_TRUE(evaluator.attemptCastingSpell(active));
		ASSERT_EQ(callback->submitted.size(), 1u);
		EXPECT_EQ(callback->submitted.front().spell, SpellID::IMPLOSION);
		const auto target = callback->submitted.front().getTarget(battle());
		ASSERT_EQ(target.size(), 1u);
		EXPECT_EQ(target.front().unitValue, valuable);
		EXPECT_EQ(valuable->getAvailableHealth(), health);
		EXPECT_TRUE(weak->alive());
		EXPECT_EQ(attackerSideHero->mana, 1000);
	}
}

TEST_F(NewHorizonsMagicAITest, ResurrectionCanonicalTargetReacquiresProjectedStateFromLiveAimIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	attackerSideHero->addSpellToSpellbook(SpellID::RESURRECTION);
	attackerSideHero->mana = 1000;
	const auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 1);
	const auto health = unit->getAvailableHealth();
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	HypotheticBattle model(environment.get(), callback->getBattle(BattleID(0)));
	auto projectedUnit = model.getForUpdate(unit->unitId());
	auto damage = health;
	projectedUnit->damage(damage);
	ASSERT_FALSE(projectedUnit->alive());
	ASSERT_TRUE(unit->alive());
	const auto * spell = SpellID(SpellID::RESURRECTION).toSpell();
	spells::BattleCast cast(&model, attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	const spells::Target aim{spells::Destination(unit)};
	const auto canonical = mechanics->canonicalizeTarget(aim);
	ASSERT_EQ(canonical.size(), 1u);
	EXPECT_EQ(canonical.front().unitValue, projectedUnit.get());
	const auto candidates = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(candidates.size(), 1u);
	ASSERT_EQ(candidates.front().size(), 1u);
	EXPECT_EQ(candidates.front().front().unitValue, projectedUnit.get());
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	cast.castEval(model.getServerCallback(), aim);
	EXPECT_TRUE(projectedUnit->alive());
	EXPECT_EQ(unit->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->mana, 1000);
}

TEST_F(NewHorizonsMagicAITest, CreatureSpellPreviewUsesCurrentVictimControlAndTracksRestoration)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	auto * caster = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 2), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(8, 5), 100);
	Bonus power;
	power.type = BonusType::CREATURE_SPELL_POWER;
	power.val = 100;
	caster->addNewBonus(std::make_shared<Bonus>(power));
	const auto * spell = SpellID(SpellID::FIREBALL).toSpell();
	const spells::Target target{spells::Destination(victim->getPosition())};
	spells::BattleCast cast(battle(), caster, spells::Mode::CREATURE_ACTIVE, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	const auto health = victim->getAvailableHealth();
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, caster, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	const auto evaluate = [&]()
	{
		PossibleSpellcast candidate;
		candidate.spell = spell;
		candidate.dest = target;
		evaluator.evaluateCreatureSpellcast(caster, candidate);
		return candidate.value;
	};
	const auto enemyDamage = evaluate();
	ASSERT_GT(enemyDamage, 0);
	auto hypnotized = std::make_shared<Bonus>();
	hypnotized->type = BonusType::HYPNOTIZED;
	hypnotized->duration = BonusDuration::ONE_BATTLE;
	hypnotized->source = BonusSource::SPELL_EFFECT;
	hypnotized->sid = BonusSourceID(SpellID(SpellID::HYPNOTIZE));
	victim->addNewBonus(hypnotized);
	ASSERT_EQ(battle()->battleGetOwner(victim), PlayerColor(0));
	// Fireball is deliberately indiscriminate; legality does not prevent collateral.
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	EXPECT_LT(evaluate(), 0);
	victim->removeBonus(hypnotized);
	ASSERT_EQ(battle()->battleGetOwner(victim), PlayerColor(1));
	EXPECT_EQ(evaluate(), enemyDamage);
	EXPECT_EQ(victim->getAvailableHealth(), health);
	EXPECT_TRUE(callback->submitted.empty());
}

TEST_F(NewHorizonsMagicAITest, RemoveObstacleEnumeratesNormalizedLocationAndRemovesOnlyInProjectedBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	attackerSideHero->addSpellToSpellbook(SpellID::REMOVE_OBSTACLE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:natureMagic")),
		3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = 1000;
	SpellCreatedObstacle obstacle;
	obstacle.uniqueID = 0;
	obstacle.ID = SpellID::FORCE_FIELD;
	obstacle.trigger = SpellID::NONE;
	obstacle.casterSide = BattleSide::DEFENDER;
	obstacle.pos = BattleHex(8, 5);
	obstacle.customSize.insert(obstacle.pos);
	BattleObstaclesChanged add;
	add.battleID = BattleID(0);
	obstacle.toInfo(add.change);
	gameHandler->sendAndApply(add);
	const auto * spell = SpellID(SpellID::REMOVE_OBSTACLE).toSpell();
	spells::BattleCast live(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&live);
	ASSERT_EQ(mechanics->getTargetTypes(), std::vector<spells::AimType>{spells::AimType::LOCATION});
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	ASSERT_EQ(targets.front().size(), 1u);
	EXPECT_EQ(targets.front().front().hexValue, obstacle.pos);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	HypotheticBattle model(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast projected(&model, attackerSideHero, spells::Mode::HERO, spell);
	projected.castEval(model.getServerCallback(), targets.front());
	EXPECT_TRUE(model.getAllObstacles().empty());
	EXPECT_TRUE(model.hasObstacleChanges());
	EXPECT_EQ(battle()->getAllObstacles().size(), 1u);
	EXPECT_EQ(battle()->getAccessibility()[obstacle.pos.toInt()], EAccessibility::OBSTACLE);
	EXPECT_EQ(attackerSideHero->mana, 1000);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::REMOVE_OBSTACLE;
	action.setTarget(targets.front());
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(battle()->getAllObstacles().empty());
	EXPECT_EQ(attackerSideHero->mana, 1000 - cost);
}

TEST_F(NewHorizonsMagicAITest, ForceFieldCastEvaluationCreatesIsolatedBlockingObstacle)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	attackerSideHero->addSpellToSpellbook(SpellID::FORCE_FIELD);
	attackerSideHero->mana = 1000;
	const auto * spell = SpellID(SpellID::FORCE_FIELD).toSpell();
	const BattleHex destination(8, 5);
	const battle::Target target{battle::Destination(destination)};
	spells::BattleCast liveCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	ASSERT_TRUE(spell->battleMechanics(&liveCast)->canBeCastAt(target));
	ASSERT_EQ(battle()->getAccessibility()[destination.toInt()], EAccessibility::ACCESSIBLE);
	ASSERT_TRUE(battle()->getAllObstacles().empty());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	HypotheticBattle model(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast projected(&model, attackerSideHero, spells::Mode::HERO, spell);
	projected.castEval(model.getServerCallback(), target);
	EXPECT_FALSE(model.getAllObstacles().empty());
	EXPECT_TRUE(model.hasObstacleChanges());
	EXPECT_EQ(model.getAccessibility()[destination.toInt()], EAccessibility::OBSTACLE);
	EXPECT_TRUE(battle()->getAllObstacles().empty());
	EXPECT_EQ(attackerSideHero->mana, 1000);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::FORCE_FIELD;
	action.setTarget(target);
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getAccessibility()[destination.toInt()], EAccessibility::OBSTACLE);
	EXPECT_EQ(attackerSideHero->mana, 1000 - cost);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
}

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

TEST_F(NewHorizonsMagicAITest, SelectiveDispelPreservesBeneficialEffectWhenFullDispelWouldLoseIt)
{
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	const auto sorcery = SecondarySkill::decode("new-horizons:sorceryMagic");
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), 2, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:sorceryMagic",
		"new-horizons:sorceryMagic.selectiveDispel"});
	attackerSideHero->mana = 1000;
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel"));
	ASSERT_NO_FATAL_FAILURE(startBattle());

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(4, 5), 10000);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	Bonus blessed(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL,
		BonusSource::SPELL_EFFECT, 50, BonusSourceID(SpellID(SpellID::BLESS)), BonusSubtypeID(PrimarySkill::ATTACK));
	blessed.turnsRemain = 3;
	Bonus cursed(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL,
		BonusSource::SPELL_EFFECT, -50, BonusSourceID(SpellID(SpellID::CURSE)), BonusSubtypeID(PrimarySkill::ATTACK));
	cursed.turnsRemain = 3;
	Bonus blessedSpeed(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::BLESS)));
	blessedSpeed.turnsRemain = 3;
	Bonus cursedSpeed(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, -1, BonusSourceID(SpellID(SpellID::CURSE)));
	cursedSpeed.turnsRemain = 3;
	SetStackEffect effects;
	effects.battleID = BattleID(0);
	effects.toAdd.emplace_back(active->unitId(), std::vector<Bonus>{blessed, cursed, blessedSpeed, cursedSpeed});
	gameHandler->sendAndApply(effects);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_TRUE(active->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)))));
	ASSERT_TRUE(active->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::CURSE)))));

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	const auto * dispel = SpellID(SpellID::DISPEL).toSpell();
	for(const bool selective : {false, true})
	{
		SCOPED_TRACE(selective ? "selective" : "full");
		auto model = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		const auto * projectedTarget = model->battleGetUnitByID(active->unitId());
		ASSERT_NE(projectedTarget, nullptr);
		ASSERT_TRUE(projectedTarget->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)))));
		ASSERT_TRUE(projectedTarget->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::CURSE)))));
		const spells::Target aim{spells::Destination(projectedTarget)};
		spells::BattleCast cast(model.get(), attackerSideHero, spells::Mode::HERO, dispel);
		cast.setSelectiveDispel(selective);
		if(selective)
		{
			spells::detail::ProblemImpl problem;
			const bool legal = dispel->battleMechanics(&cast)->canBeCastAt(aim, problem);
			std::vector<std::string> messages;
			problem.getAll(messages);
			ASSERT_TRUE(legal) << (messages.empty() ? "no diagnostic" : messages.front());
		}
		cast.castEval(model->getServerCallback(), aim);
		const auto * projected = model->battleGetUnitByID(active->unitId());
		ASSERT_NE(projected, nullptr);
		EXPECT_EQ(projected->getAttack(false), selective ? active->getAttack(false) + 50 : active->getAttack(false));
	}
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, SpellID::DISPEL);
	EXPECT_TRUE(action.spellSelectiveDispel);
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	EXPECT_EQ(target.front().unitValue, active);
	EXPECT_EQ(attackerSideHero->mana, 1000);
	EXPECT_TRUE(active->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)))));
	EXPECT_TRUE(active->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::CURSE)))));
}

TEST_F(NewHorizonsMagicAITest, TemporalFieldAIChoosesMassSlowWithoutMutatingLiveBattle)
{
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
	const auto sorcery = SecondarySkill::decode("new-horizons:sorceryMagic");
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), 2, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:sorceryMagic",
		"new-horizons:sorceryMagic.temporalField"});
	attackerSideHero->mana = 1000;
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField"));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
	auto * enemyA = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(10, 5), 1000);
	auto * enemyB = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
	auto * enemyC = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(14, 5), 1000);
	auto * enemyD = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(10, 7), 1000);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemyA && unit != enemyB && unit != enemyC && unit != enemyD)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -active->getMovementRange();
	active->addNewBonus(std::make_shared<Bonus>(immobilized));
	ASSERT_EQ(active->getMovementRange(), 0);
	ASSERT_FALSE(active->canShoot());
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	const auto manaBefore = attackerSideHero->mana;
	const auto enemyAHealthBefore = enemyA->getAvailableHealth();
	const auto enemyBHealthBefore = enemyB->getAvailableHealth();
	const bool temporalFieldBefore = battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed;
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, SpellID::SLOW);
	EXPECT_TRUE(action.spellMassSlow);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_FALSE(action.target.front().hexValue.isValid());
	EXPECT_LT(action.target.front().unitValue, 0);
	EXPECT_EQ(attackerSideHero->mana, manaBefore);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed, temporalFieldBefore);
	EXPECT_EQ(enemyA->getAvailableHealth(), enemyAHealthBefore);
	EXPECT_EQ(enemyB->getAvailableHealth(), enemyBHealthBefore);
	const auto slowEffect = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW)));
	EXPECT_FALSE(enemyA->hasBonus(slowEffect));
	EXPECT_FALSE(enemyB->hasBonus(slowEffect));
	EXPECT_FALSE(enemyC->hasBonus(slowEffect));
	EXPECT_FALSE(enemyD->hasBonus(slowEffect));
}

TEST_F(NewHorizonsMagicAITest, TemporalFieldAIRespectsConsumedBudget)
{
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
	const auto sorcery = SecondarySkill::decode("new-horizons:sorceryMagic");
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), 2, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:sorceryMagic",
		"new-horizons:sorceryMagic.temporalField"});
	attackerSideHero->mana = 1000;
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(10, 5), 1000);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -active->getMovementRange();
	active->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed = true;

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, SpellID::SLOW);
	EXPECT_FALSE(callback->submitted.front().spellMassSlow);
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
