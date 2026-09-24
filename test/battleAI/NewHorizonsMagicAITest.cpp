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
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../lib/spells/Problem.h"

#include <chrono>

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

std::string describeMagicAIState(const MagicCallback & callback, const JsonNode & magicRules,
	const JsonNode & heroCommandRules)
{
	std::ostringstream out;
	out << "submitted actions: ";
	if(callback.submitted.empty())
		out << "<none>";
	for(size_t index = 0; index < callback.submitted.size(); ++index)
	{
		if(index != 0)
			out << "; ";
		const auto & action = callback.submitted[index];
		out << '#' << index << "{actionType=" << static_cast<int>(action.actionType)
			<< ", command=" << heroCommands::key(action.command) << '(' << static_cast<int>(action.command) << ')'
			<< ", spell=" << action.spell.getNum()
			<< ", cureAffliction=" << action.spellCureAffliction.getNum()
			<< ", targetCount=" << action.target.size()
			<< ", metamagicFollowup=" << action.metamagicFollowup
			<< ", metamagicGrand=" << action.metamagicGrand
			<< ", metamagicDecline=" << action.metamagicDecline << '}';
	}
	const auto rulesetVersion = [](const JsonNode & rules)
	{
		return rules["rulesetVersion"].isNumber()
			? std::to_string(rules["rulesetVersion"].Integer()) : std::string("<absent>");
	};
	const auto entryCount = [](const JsonNode & node)
	{
		return node.isStruct() ? node.Struct().size() : size_t{0};
	};
	out << "; magicRules={type=" << static_cast<int>(magicRules.getType())
		<< ", version=" << rulesetVersion(magicRules)
		<< ", spellRows=" << entryCount(magicRules["spells"])
		<< "}; heroCommandRules={type=" << static_cast<int>(heroCommandRules.getType())
		<< ", version=" << rulesetVersion(heroCommandRules)
		<< ", commands=" << entryCount(heroCommandRules["commands"]) << '}';
	return out.str();
}
}

namespace
{
constexpr auto transfigureMatterKey = "new-horizons:transfigureMatter";
constexpr auto matterShaperPerk = "new-horizons:sorceryMagic.matterShaper";

SpellID transfigureMatterSpell()
{
	return SpellID(SpellID::decode(transfigureMatterKey));
}
}

class NewHorizonsMagicAITest : public HeroCommandFixture
{
protected:
	bool useCurrentMagicRules = false;
	bool useLegacyMagicRules = false;
	bool neutralizeCommandEffects = false;
	bool useSavedPerkRules = false;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(useLegacyMagicRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		else if(useCurrentMagicRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
				JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		if(useSavedPerkRules)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(neutralizeCommandEffects)
		{
			const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
			JsonNode rules = combatRules["combat"]["heroCommands"];
			for(auto & commandEntry : rules["commands"].Struct())
				for(auto & effectEntry : commandEntry.second["effects"].Struct())
				{
					auto & formula = effectEntry.second;
					formula["base"].Float() = 0;
					formula["attack"].Float() = 0;
					formula["defense"].Float() = 0;
				}
			heroCommands::validateRules(rules);
			loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
		}
	}

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
	setTestSpellPointTotal(attackerSideHero, 1000);
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
		EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);
	}
}

TEST_F(NewHorizonsMagicAITest, RepeatedMovementEvaluationWithSavedPerksIsStableAndReadOnly)
{
	useCommands = false;
	useSavedPerkRules = true;
	ASSERT_NO_FATAL_FAILURE(startGame());

	const auto offenseId = SecondarySkill::decode("new-horizons:offense");
	ASSERT_GE(offenseId, 0);
	const SecondarySkill offense(offenseId);
	attackerSideHero->setSecSkillLevel(offense, 2, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:offense", "new-horizons:offense.executioner"});
	attackerSideHero->applyPerkSelection({
		"new-horizons:offense", "new-horizons:offense.armorPiercer"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:offense", "new-horizons:offense.executioner"));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:offense", "new-horizons:offense.armorPiercer"));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(
		"new-horizons:offense", "new-horizons:offense.breakthrough"));

	attackerSideHero->setSecSkillLevel(offense, 1, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(
		"new-horizons:offense", "new-horizons:offense.armorPiercer"));
	EXPECT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:offense", "new-horizons:offense.executioner"));
	attackerSideHero->setSecSkillLevel(offense, 2, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:offense", "new-horizons:offense.armorPiercer"));

	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 10);
	auto * enemyA = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(10, 5), 100);
	auto * enemyB = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(14, 5), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemyA && unit != enemyB)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	ASSERT_GT(active->getMovementRange(), 0);
	ASSERT_FALSE(active->hasBonusOfType(BonusType::FLYING));
	ASSERT_EQ(battle()->battleGetOwnerHero(active), attackerSideHero);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), active->unitId());

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedActive = model->battleGetUnitByID(active->unitId());
	ASSERT_NE(projectedActive, nullptr);
	PotentialTargets targets(projectedActive, damageCache, model);
	EXPECT_TRUE(targets.possibleAttacks.empty());
	ASSERT_GE(targets.unreachableEnemies.size(), 2u);

	const auto stateBefore = gameState()->saveToMemory();
	const auto roundBefore = battle()->battleGetRound();
	const auto activeUnitBefore = battle()->battleActiveUnit()->unitId();
	auto snapshotBattleUnits = [&]()
	{
		std::vector<std::tuple<uint32_t, int32_t, int32_t, int64_t>> snapshot;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			snapshot.emplace_back(unit->unitId(), unit->getPosition().toInt(),
				unit->getCount(), unit->getAvailableHealth());
		std::ranges::sort(snapshot);
		return snapshot;
	};
	const auto battleUnitsBefore = snapshotBattleUnits();

	BattleExchangeEvaluator evaluator(model, environment, 1.0f, 2);
	constexpr int sampleCount = 3;
	std::array<MoveTarget, sampleCount> decisions;
	std::array<int64_t, sampleCount> sampleMicros{};
	for(int sample = 0; sample < sampleCount; ++sample)
	{
		const auto started = std::chrono::steady_clock::now();
		decisions[sample] = evaluator.findMoveTowardsUnreachable(
			projectedActive, targets, damageCache, model);
		const auto finished = std::chrono::steady_clock::now();
		sampleMicros[sample] = std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count();
	}

	ASSERT_FALSE(decisions.front().positions.empty());
	EXPECT_NE(decisions.front().score, EvaluationResult::INEFFECTIVE_SCORE);
	EXPECT_GT(decisions.front().turnsToReach, 1);
	for(int sample = 1; sample < sampleCount; ++sample)
	{
		EXPECT_FLOAT_EQ(decisions[sample].score, decisions.front().score);
		EXPECT_EQ(decisions[sample].positions, decisions.front().positions);
		EXPECT_EQ(decisions[sample].turnsToReach, decisions.front().turnsToReach);
	}
	EXPECT_EQ(gameState()->saveToMemory(), stateBefore);
	EXPECT_EQ(battle()->battleGetRound(), roundBefore);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), activeUnitBefore);
	EXPECT_EQ(snapshotBattleUnits(), battleUnitsBefore);

	std::ostringstream sampleTimes;
	for(int sample = 0; sample < sampleCount; ++sample)
	{
		if(sample != 0)
			sampleTimes << ',';
		sampleTimes << sampleMicros[sample];
	}
	RecordProperty("movementEvaluatorSampleCount", sampleCount);
	RecordProperty("movementEvaluatorSampleMicros", sampleTimes.str());
}

TEST_F(NewHorizonsMagicAITest, PhantomArmyValuesTemporaryCombatPowerAndChoosesTheStrongestLegalSource)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(startGame());

	const auto phantomArmy = SpellID(SpellID::decode("new-horizons:phantomArmy"));
	ASSERT_NE(phantomArmy, SpellID::NONE);
	const auto * spell = phantomArmy.toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_EQ(spell->getJsonKey(), newHorizonsSorcery::PHANTOM_ARMY_SPELL);

	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto knownSpell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(knownSpell);
	attackerSideHero->addSpellToSpellbook(phantomArmy);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto sorcery = SecondarySkill::decode("new-horizons:sorceryMagic");
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), 3, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 1);
	auto * strongSource = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 4), 100);
	auto * weakSource = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(4, 5), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != strongSource && unit != weakSource && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS);
	const auto ordinaryAction = evaluator.selectStackAction(active);
	ASSERT_EQ(ordinaryAction.actionType, EActionType::SHOOT);

	// The ordinary shot gives the spell evaluator a real pass/attack baseline.
	// An unvalued temporary summon ties that baseline and is rejected; the
	// Phantom's copied combat power must make the spell the better hero action.
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, phantomArmy);
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	EXPECT_EQ(target.front().unitValue, strongSource);

	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000 - battle()->battleGetSpellCost(spell, attackerSideHero));
}

TEST_F(NewHorizonsMagicAITest, CounterspellAIArmsAThreatWardAndSkipsAnAlreadyArmedWard)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto counterspell = SpellID(SpellID::decode("new-horizons:counterspell"));
	ASSERT_TRUE(counterspell.hasValue());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto attackerSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : attackerSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	const auto defenderSpells = defenderSideHero->getSpellsInSpellbook();
	for(const auto spell : defenderSpells)
		defenderSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(counterspell);
	defenderSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	setTestSpellPointTotal(attackerSideHero, 100);
	setTestSpellPointTotal(defenderSideHero, 100);
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit->unitSide() == BattleSide::ATTACKER)
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

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	ASSERT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(
		*callback->getBattle(BattleID(0)), counterspell));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(counterspell.toSpell()));
	ASSERT_EQ(callback->getBattle(BattleID(0))->battleCanCastSpell(
		attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	ASSERT_TRUE(counterspell.toSpell()->canBeCast(
		callback->getBattle(BattleID(0)).get(), spells::Mode::HERO, attackerSideHero));
	ASSERT_TRUE(defenderSideHero->canCastThisSpell(SpellID(SpellID::IMPLOSION).toSpell()));
	const auto implosionLevel = battle()->battleGetSpellLevel(SpellID::IMPLOSION);
	ASSERT_GT(implosionLevel, 0);
	auto blockImplosion = std::make_shared<Bonus>();
	blockImplosion->duration = BonusDuration::ONE_BATTLE;
	blockImplosion->type = BonusType::BLOCK_MAGIC_ABOVE;
	blockImplosion->val = implosionLevel - 1;
	defenderSideHero->addNewBonus(blockImplosion);
	BattleEvaluator blockedEvaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	blockedEvaluator.selectStackAction(active);
	EXPECT_FALSE(blockedEvaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
	defenderSideHero->removeBonus(blockImplosion);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, counterspell);
	ASSERT_EQ(callback->submitted.front().target.size(), 1u);
	EXPECT_FALSE(callback->submitted.front().target.front().hexValue.isValid());
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->submitted.front()));
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);

	// The first cast consumed this turn's hero-spell budget. Reset only that
	// budget in the fixture, preserving the authoritative armed ward, so this
	// assertion exercises the redundant-ward guard rather than the turn limit.
	auto & attackerState = battle()->getSide(BattleSide::ATTACKER);
	attackerState.castSpellsCount = 0;
	attackerState.heroCommandUsed = false;
	ASSERT_TRUE(attackerState.counterspellArmed);

	callback->submitted.clear();
	BattleEvaluator armedEvaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	armedEvaluator.selectStackAction(active);
	EXPECT_FALSE(armedEvaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
}

TEST_F(NewHorizonsMagicAITest, CounterspellAIRecognizesEnemyHatOnlyLevelFiveSpellThreat)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto counterspell = SpellID(SpellID::decode("new-horizons:counterspell"));
	const auto armageddon = SpellID(SpellID::decode("core:armageddon"));
	const ArtifactID spellbindersHat(ArtifactID::decode("core:spellbindersHat"));
	ASSERT_TRUE(counterspell.hasValue());
	ASSERT_TRUE(armageddon.hasValue());
	ASSERT_TRUE(spellbindersHat.hasValue());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->removeAllSpells();
	defenderSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(counterspell);
	ASSERT_TRUE(defenderSideHero->getSpellsInSpellbook().empty());
	ASSERT_EQ(defenderSideHero->getSpellLevel(armageddon.toSpell()), 5);
	giveArtifact(defenderSideHero, spellbindersHat, ArtifactPosition::HEAD);
	ASSERT_NE(defenderSideHero->getArt(ArtifactPosition::HEAD), nullptr);
	ASSERT_EQ(defenderSideHero->getArt(ArtifactPosition::HEAD)->getTypeId(), spellbindersHat);
	ASSERT_FALSE(defenderSideHero->spellbookContainsSpell(armageddon));
	ASSERT_TRUE(vstd::contains(defenderSideHero->getInscribedSpellsForCasting(), armageddon));
	setTestSpellPointTotal(attackerSideHero, 100);
	setTestSpellPointTotal(defenderSideHero, 100);
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit->unitSide() == BattleSide::ATTACKER)
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

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	ASSERT_TRUE(defenderSideHero->hasSpellbook());
	ASSERT_TRUE(defenderSideHero->canCastThisSpell(armageddon.toSpell()));
	ASSERT_GE(defenderSideHero->getManaAvailable(),
		battle()->battleGetSpellCost(armageddon.toSpell(), defenderSideHero));
	const auto counterspellCost = battle()->battleGetSpellCost(counterspell.toSpell(), attackerSideHero);
	const auto armageddonWardCost = newHorizonsMagic::counterspellCost(
		defenderSideHero->getListedSpellCost(armageddon.toSpell()), false);
	ASSERT_GE(attackerSideHero->getManaAvailable() - counterspellCost, armageddonWardCost);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, counterspell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->submitted.front()));
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);

	ASSERT_TRUE(gameHandler->moveArtifact(PlayerColor(1),
		ArtifactLocation(defenderSideHero->id, ArtifactPosition::HEAD),
		ArtifactLocation(defenderSideHero->id, ArtifactPosition::BACKPACK_START)));
	EXPECT_FALSE(defenderSideHero->isSpellInscribedForCasting(armageddon));
	EXPECT_FALSE(vstd::contains(defenderSideHero->getInscribedSpellsForCasting(), armageddon));

	// Clear only the state produced by the accepted cast so the next evaluation
	// isolates the removed artifact's threat contribution.
	auto & attackerState = battle()->getSide(BattleSide::ATTACKER);
	attackerState.counterspellArmed = false;
	attackerState.castSpellsCount = 0;
	attackerState.heroCommandUsed = false;
	callback->submitted.clear();
	BattleEvaluator noHatEvaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	noHatEvaluator.selectStackAction(active);
	EXPECT_FALSE(noHatEvaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
}

TEST_F(NewHorizonsMagicAITest, MetamagicAIRetainsAllowanceInsteadOfCastingHarmfulFollowup)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(startGame());

	const auto metamagic = SecondarySkill::decode("new-horizons:metamagic");
	ASSERT_GE(metamagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(metamagic), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	setTestSpellPointTotal(attackerSideHero, 1000);

	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(4, 5), 10000);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	// Dispel is legal on this friendly Bless, but removing it lowers the
	// active exchange's attack score. The absolute follow-up score remains
	// positive; only comparison with the no-cast baseline can reject it.
	active->addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, 50,
		BonusSourceID(SpellID(SpellID::BLESS)), BonusSubtypeID(PrimarySkill::ATTACK)));

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	// This is the state after the ordinary triggering spell.  The follow-up is
	// legal even though the ordinary hero-spell budget is already spent.
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.castSpellsCount = 1;
	side.metamagicPendingCount = 1;
	side.metamagicFirstSpell = SpellID::HASTE;
	side.metamagicFirstTargetUnitId = newHorizonsMagic::INVALID_METAMAGIC_TARGET;
	side.metamagicSequenceSpells = {SpellID::HASTE};

	const auto * dispel = SpellID(SpellID::DISPEL).toSpell();
	spells::BattleCast legal(battle(), attackerSideHero, spells::Mode::HERO, dispel);
	legal.setMetamagicFollowup(true);
	legal.setMetamagicTargetUnitId(active->unitId());
	ASSERT_TRUE(dispel->battleMechanics(&legal)->canBeCastAt({spells::Destination(active)}));

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	EXPECT_FALSE(evaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_EQ(side.metamagicPendingCount, 1);
	EXPECT_EQ(side.metamagicUsesConsumed, 0);
}

TEST_F(NewHorizonsMagicAITest, MetamagicAIUsesOrdinaryRepeatedSpellAndLeavesGrandAvailable)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(startGame());

	const auto metamagic = SecondarySkill::decode("new-horizons:metamagic");
	ASSERT_GE(metamagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(metamagic), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.grandMetamagic"});
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	setTestSpellPointTotal(attackerSideHero, 1000);

	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto & metamagicState = battle()->getSide(BattleSide::ATTACKER);
	metamagicState.castSpellsCount = 1;
	metamagicState.metamagicPendingCount = 1;
	metamagicState.metamagicFirstSpell = SpellID::IMPLOSION;
	metamagicState.metamagicFirstTargetUnitId = enemy->unitId();
	metamagicState.metamagicSequenceSpells = {SpellID::IMPLOSION};

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, SpellID::IMPLOSION);
	EXPECT_TRUE(callback->submitted.front().metamagicFollowup);
	EXPECT_FALSE(callback->submitted.front().metamagicGrand);
	EXPECT_EQ(metamagicState.metamagicPendingCount, 1);
	EXPECT_FALSE(metamagicState.metamagicGrandUsed);
}

TEST_F(NewHorizonsMagicAITest, MetamagicAIChoosesGrandForAHighValueDistinctAlternative)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(startGame());

	const auto metamagic = SecondarySkill::decode("new-horizons:metamagic");
	ASSERT_GE(metamagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(metamagic), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.grandMetamagic"});
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
	attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	attackerSideHero->addSpellToSpellbook(SpellID::FIREBALL);
	setTestSpellPointTotal(attackerSideHero, 1000);

	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10000);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto & metamagicState = battle()->getSide(BattleSide::ATTACKER);
	metamagicState.castSpellsCount = 1;
	metamagicState.metamagicPendingCount = 1;
	metamagicState.metamagicFirstSpell = SpellID::HASTE;
	metamagicState.metamagicFirstTargetUnitId = newHorizonsMagic::INVALID_METAMAGIC_TARGET;
	metamagicState.metamagicSequenceSpells = {SpellID::HASTE};

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, SpellID::IMPLOSION);
	EXPECT_TRUE(callback->submitted.front().metamagicFollowup);
	EXPECT_TRUE(callback->submitted.front().metamagicGrand);
}

TEST_F(NewHorizonsMagicAITest, ResurrectionCanonicalTargetReacquiresProjectedStateFromLiveAimIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	attackerSideHero->addSpellToSpellbook(SpellID::RESURRECTION);
	setTestSpellPointTotal(attackerSideHero, 1000);
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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);
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
	setTestSpellPointTotal(attackerSideHero, 1000);
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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::REMOVE_OBSTACLE;
	action.setTarget(targets.front());
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(battle()->getAllObstacles().empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000 - cost);
}

TEST_F(NewHorizonsMagicAITest, CanonicalFireWallAIProducesCompactOrientedActionServerAcceptsIt)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::FIRE_WALL);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -static_cast<int32_t>(active->getMovementRange());
	active->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	const auto obstaclesBefore = battle()->obstacles.size();
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, SpellID::FIRE_WALL);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().unitValue, -1000);
	ASSERT_TRUE(action.target.front().hexValue.isAvailable());
	EXPECT_NE(action.spellFireWallDirection, BattleHex::NONE);

	// The AI's in-memory target is a complete line, but the wire action must
	// carry only its start hex and direction. Reconstruct that same line here
	// and prove each tile is empty before handing the compact action to the
	// authoritative processor.
	spells::Target footprint;
	BattleHex current = action.target.front().hexValue;
	for(int index = 0; index < 3; ++index)
	{
		ASSERT_TRUE(current.isAvailable());
		EXPECT_EQ(battle()->battleGetUnitByPos(current, true), nullptr);
		EXPECT_TRUE(battle()->battleGetAllObstaclesOnPos(current, false).empty());
		footprint.emplace_back(current);
		if(index != 2)
			current = current.cloneInDirection(action.spellFireWallDirection, false);
	}
	ASSERT_EQ(footprint.size(), 3u);
	EXPECT_TRUE(std::all_of(footprint.begin(), footprint.end(), [&](const auto & destination)
	{
		return destination.hexValue.isAvailable();
	}));
	EXPECT_TRUE(std::any_of(footprint.begin(), footprint.end(), [&](const auto & destination)
	{
		return BattleHex::getDistance(destination.hexValue, enemy->getPosition()) == 1;
	}));

	const auto * spell = SpellID(SpellID::FIRE_WALL).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(footprint));
	EXPECT_GT(SpellTargetEvaluator::fireWallPlacementValue(mechanics.get(), footprint), 0.0f);

	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	ASSERT_EQ(battle()->obstacles.size(), obstaclesBefore + 1);
	const auto wallIt = std::find_if(battle()->obstacles.begin(), battle()->obstacles.end(), [](const auto & obstacle)
	{
		const auto * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
		return spellObstacle && spellObstacle->ID == SpellID::FIRE_WALL;
	});
	ASSERT_NE(wallIt, battle()->obstacles.end());
	const auto * wall = dynamic_cast<const SpellCreatedObstacle *>(wallIt->get());
	ASSERT_NE(wall, nullptr);
	EXPECT_EQ(wall->customSize.size(), 3u);
	for(const auto & destination : footprint)
		EXPECT_TRUE(wall->customSize.contains(destination.hexValue));
	EXPECT_TRUE(wall->damageSnapshot);
	EXPECT_TRUE(wall->passable);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 12);
}

TEST_F(NewHorizonsMagicAITest, CanonicalFireWallAIAvoidsFriendlyGroundExposure)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::FIRE_WALL);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	// This ally sits on the most direct upper approach to the enemy. There are
	// equally close empty lines on the opposite side, which should remain safe.
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(11, 4), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != ally && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -static_cast<int32_t>(active->getMovementRange());
	active->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.spell, SpellID::FIRE_WALL);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_NE(action.spellFireWallDirection, BattleHex::NONE);

	BattleHex current = action.target.front().hexValue;
	for(int index = 0; index < 3; ++index)
	{
		// A wall tile adjacent to the friendly ground stack is an exposed line:
		// walking through it would trigger the same damage as for an enemy.
		EXPECT_GT(BattleHex::getDistance(current, ally->getPosition()), 1);
		if(index != 2)
			current = current.cloneInDirection(action.spellFireWallDirection, false);
	}
}

TEST_F(NewHorizonsMagicAITest, CanonicalFireWallAIRejectsZeroHostilePressure)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::FIRE_WALL);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -static_cast<int32_t>(active->getMovementRange());
	active->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	EXPECT_FALSE(evaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
}

TEST_F(NewHorizonsMagicAITest, LandMineAIUsesExactSpellPowerCountAndOnlyLiveLegalHexes)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	ASSERT_NE(spell, nullptr);
	const auto obstaclesBefore = battle()->obstacles.size();
	for(const auto [power, required] : std::array<std::pair<int, int>, 5>{
		std::pair{0, 2}, std::pair{99, 2}, std::pair{100, 3}, std::pair{199, 3}, std::pair{200, 4}})
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		cast.setEffectPower(power);
		const auto mechanics = spell->battleMechanics(&cast);
		ASSERT_TRUE(newHorizonsMagic::rulesActive(battle()->getMagicRules()));
		ASSERT_EQ(mechanics->getEffectPower(), power);
		const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
		ASSERT_EQ(targets.size(), 1u);
		ASSERT_EQ(targets.front().size(), static_cast<size_t>(required));
		std::set<int> selected;
		for(const auto & destination : targets.front())
		{
			ASSERT_EQ(destination.unitValue, nullptr);
			ASSERT_TRUE(destination.hexValue.isValid());
			EXPECT_TRUE(selected.insert(destination.hexValue.toInt()).second);
		}
		EXPECT_TRUE(mechanics->canBeCastAt(targets.front()));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);
		EXPECT_EQ(battle()->obstacles.size(), obstaclesBefore);
	}

	// The ordered vector is a deterministic AI contract, not an unordered set.
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	spells::BattleCast firstCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	spells::BattleCast secondCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto first = SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&firstCast).get());
	const auto second = SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&secondCast).get());
	ASSERT_EQ(first.size(), 1u);
	ASSERT_EQ(second.size(), 1u);
	ASSERT_EQ(first.front().size(), second.front().size());
	for(size_t index = 0; index < first.front().size(); ++index)
		EXPECT_EQ(first.front()[index].hexValue, second.front()[index].hexValue);
	(void)active;
}

TEST_F(NewHorizonsMagicAITest, LandMineAISelectsHostileGroundApproachPressureAndServerAcceptsAction)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -static_cast<int32_t>(active->getMovementRange());
	active->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	// Use a callback owned by the evaluator so the submitted action is directly
	// observable without mutating the authoritative battle during evaluation.
	auto recordingCallback = std::make_shared<MagicCallback>();
	recordingCallback->onBattleStarted(battle());
	BattleEvaluator recordingEvaluator(environment, recordingCallback, active,
		PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	recordingEvaluator.selectStackAction(active);
	ASSERT_TRUE(recordingEvaluator.attemptCastingSpell(active));
	ASSERT_EQ(recordingCallback->submitted.size(), 1u);
	const auto & action = recordingCallback->submitted.front();
	EXPECT_EQ(action.spell, SpellID::LAND_MINE);
	ASSERT_EQ(action.target.size(), 2u);
	std::set<int> selected;
	for(const auto & destination : action.target)
	{
		EXPECT_EQ(destination.unitValue, -1000);
		EXPECT_TRUE(selected.insert(destination.hexValue.toInt()).second);
		EXPECT_EQ(battle()->battleGetUnitByPos(destination.hexValue, true), nullptr);
		EXPECT_TRUE(battle()->battleGetAllObstaclesOnPos(destination.hexValue, false).empty());
		EXPECT_EQ(battle()->getAccessibility()[destination.hexValue.toInt()], EAccessibility::ACCESSIBLE);
	}
	EXPECT_TRUE(std::any_of(action.target.begin(), action.target.end(), [&](const auto & destination)
	{
		return BattleHex::getDistance(destination.hexValue, enemy->getPosition()) == 1;
	}));

	const auto obstaclesBefore = battle()->obstacles.size();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_GE(battle()->obstacles.size(), obstaclesBefore + action.target.size());
}

TEST_F(NewHorizonsMagicAITest, LandMinePlacementIgnoresImmuneClusterForSusceptibleApproach)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * susceptible = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	std::vector<const CStack *> immuneCluster;
	for(const auto position : {BattleHex(14, 2), BattleHex(14, 3), BattleHex(14, 4)})
	{
		auto * immune = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), position, 10);
		immune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
			BonusSubtypeID(SpellID(SpellID::LAND_MINE))));
		immuneCluster.push_back(immune);
	}

	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != susceptible
			&& std::find(immuneCluster.begin(), immuneCluster.end(), unit) == immuneCluster.end())
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setEffectPower(43);
	const auto mechanics = spell->battleMechanics(&cast);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	ASSERT_EQ(targets.front().size(), 2u);
	EXPECT_TRUE(std::any_of(targets.front().begin(), targets.front().end(), [&](const auto & destination)
	{
		return BattleHex::getDistance(destination.hexValue, susceptible->getPosition()) == 1;
	}));
	EXPECT_FALSE(std::any_of(targets.front().begin(), targets.front().end(), [&](const auto & destination)
	{
		return std::any_of(immuneCluster.begin(), immuneCluster.end(), [&](const auto * immune)
		{
			return BattleHex::getDistance(destination.hexValue, immune->getPosition()) == 1;
		});
	}));
}

TEST_F(NewHorizonsMagicAITest, LandMinePlacementUsesDamageScaleAndSkipsTinyOrImmuneTargets)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * tiny = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != tiny)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	spells::BattleCast tinyCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	tinyCast.setEffectPower(43);
	const auto tinyMechanics = spell->battleMechanics(&tinyCast);
	const auto tinyTargets = SpellTargetEvaluator::getViableTargets(tinyMechanics.get());
	ASSERT_EQ(tinyTargets.size(), 1u);
	ASSERT_EQ(tinyTargets.front().size(), 2u);
	const auto tinyValue = SpellTargetEvaluator::landMinePlacementValue(
		tinyMechanics.get(), tinyTargets.front());
	ASSERT_GT(tinyValue, 0.0f);

	// Permanent spell immunity must remove the delayed contribution entirely,
	// rather than allowing an immune stack to dominate placement ranking.
	tiny->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::LAND_MINE))));
	ASSERT_TRUE(tiny->hasImmunity(SpellID(SpellID::LAND_MINE)));
	ASSERT_EQ(tinyMechanics->getSpellId(), SpellID(SpellID::LAND_MINE));
	ASSERT_TRUE(battle()->battleGetUnitByID(tiny->unitId())->hasImmunity(SpellID(SpellID::LAND_MINE)));
	EXPECT_EQ(SpellTargetEvaluator::landMinePlacementValue(tinyMechanics.get(), tinyTargets.front()), 0.0f);

	// Replace the one-creature target with a healthy stack at the same approach
	// point.  The common AttackPossibility reduction scale must account for the
	// actual health/value at risk, not the mine's raw cast damage alone.
	BattleUnitsChanged removeTiny;
	removeTiny.battleID = BattleID(0);
	removeTiny.changedStacks.emplace_back(tiny->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(removeTiny);
	addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	spells::BattleCast largeCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	largeCast.setEffectPower(43);
	const auto largeMechanics = spell->battleMechanics(&largeCast);
	const auto largeTargets = SpellTargetEvaluator::getViableTargets(largeMechanics.get());
	ASSERT_EQ(largeTargets.size(), 1u);
	ASSERT_EQ(largeTargets.front().size(), 2u);
	const auto largeValue = SpellTargetEvaluator::landMinePlacementValue(
		largeMechanics.get(), largeTargets.front());
	EXPECT_GT(largeValue, tinyValue);
}

TEST_F(NewHorizonsMagicAITest, LandMinePlacementCountsEachConsumableMineOnceAcrossEnemies)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * firstEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != firstEnemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	const spells::Target target{
		spells::Destination(BattleHex(11, 5)),
		spells::Destination(BattleHex(11, 6))};
	spells::BattleCast firstCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	firstCast.setEffectPower(43);
	const auto firstMechanics = spell->battleMechanics(&firstCast);
	ASSERT_TRUE(firstMechanics->canBeCastAt(target));
	const auto oneEnemyValue = SpellTargetEvaluator::landMinePlacementValue(firstMechanics.get(), target);
	ASSERT_GT(oneEnemyValue, 0.0f);

	// Both mines are adjacent to the first stack.  The extra stacks can also
	// reach one of the selected tiles, but they must not make either consumable
	// mine worth three independent delayed hits.
	addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 6), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(13, 5), 10);
	spells::BattleCast manyCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	manyCast.setEffectPower(43);
	const auto manyMechanics = spell->battleMechanics(&manyCast);
	const auto manyEnemyValue = SpellTargetEvaluator::landMinePlacementValue(manyMechanics.get(), target);
	EXPECT_LE(manyEnemyValue, oneEnemyValue * 2.05f);
}

TEST_F(NewHorizonsMagicAITest, LandMinePlacementUsesMaximumWeightDistinctMineAssignment)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * firstEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != firstEnemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	const spells::Target target{
		spells::Destination(BattleHex(11, 6)),
		spells::Destination(BattleHex(11, 5))};
	spells::BattleCast firstCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	firstCast.setEffectPower(43);
	const auto firstMechanics = spell->battleMechanics(&firstCast);
	ASSERT_TRUE(firstMechanics->canBeCastAt(target));
	const auto oneEnemyValue = SpellTargetEvaluator::landMinePlacementValue(firstMechanics.get(), target);
	ASSERT_GT(oneEnemyValue, 0.0f);

	// A is adjacent to both mines, while B is adjacent only to the first mine
	// and can reach the second one at the lower approach likelihood.  The
	// maximum matching must assign A to the second mine and B to the first,
	// rather than letting A's first equal-valued choice collide with B.
	addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 6), 10);
	spells::BattleCast manyCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	manyCast.setEffectPower(43);
	const auto manyMechanics = spell->battleMechanics(&manyCast);
	const auto twoEnemyValue = SpellTargetEvaluator::landMinePlacementValue(manyMechanics.get(), target);
	EXPECT_GT(twoEnemyValue, oneEnemyValue * 1.8f);
}

TEST_F(NewHorizonsMagicAITest, LandMinePlacementIsInvariantToTargetVectorPermutation)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * firstEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	auto * secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 6), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != firstEnemy && unit != secondEnemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	const spells::Target target{
		spells::Destination(BattleHex(11, 6)),
		spells::Destination(BattleHex(11, 5))};
	const spells::Target permutedTarget{
		spells::Destination(BattleHex(11, 5)),
		spells::Destination(BattleHex(11, 6))};
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setEffectPower(43);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	ASSERT_TRUE(mechanics->canBeCastAt(permutedTarget));
	const auto targetValue = SpellTargetEvaluator::landMinePlacementValue(mechanics.get(), target);
	const auto permutedValue = SpellTargetEvaluator::landMinePlacementValue(mechanics.get(), permutedTarget);
	EXPECT_FLOAT_EQ(targetValue, permutedValue);
}

TEST_F(NewHorizonsMagicAITest, LandMineDoesNotOutrankImmediateMagicArrowOnTheSameApproach)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 43, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -static_cast<int32_t>(active->getMovementRange());
	active->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, SpellID::MAGIC_ARROW);
}

TEST_F(NewHorizonsMagicAITest, LandMineAILeavesLegacyNoTargetSelectionUntouched)
{
	useCommands = false;
	useLegacyMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_FALSE(newHorizonsMagic::rulesActive(battle()->getMagicRules()));
	EXPECT_EQ(mechanics->getTargetTypes(), std::vector<spells::AimType>{spells::AimType::NOTHING});
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	EXPECT_TRUE(targets.front().empty());
	EXPECT_EQ(SpellTargetEvaluator::landMinePlacementValue(mechanics.get(), targets.front()), 0.0f);
}

TEST_F(NewHorizonsMagicAITest, ForceFieldCastEvaluationCreatesIsolatedBlockingObstacle)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	attackerSideHero->addSpellToSpellbook(SpellID::FORCE_FIELD);
	setTestSpellPointTotal(attackerSideHero, 1000);
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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::FORCE_FIELD;
	action.setTarget(target);
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->getAccessibility()[destination.toInt()], EAccessibility::OBSTACLE);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000 - cost);
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
	setTestSpellPointTotal(attackerSideHero, 1000);
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
	const auto diagnostics = [&]
	{
		return describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	};
	// Restoring one peasant is not worth losing the only large friendly army.
	EXPECT_FALSE(evaluator.attemptCastingSpell(victim)) << diagnostics();
	EXPECT_TRUE(callback->submitted.empty()) << diagnostics();

	HypotheticBattle model(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast simulated(&model, attackerSideHero, spells::Mode::HERO, spell);
	simulated.castEval(model.getServerCallback(), target);
	EXPECT_TRUE(model.battleGetUnitByID(corpse->unitId())->alive());
	EXPECT_TRUE(model.battleGetUnitByID(victim->unitId())->isGhost());
	EXPECT_EQ(model.battleGetUnitByID(victim->unitId())->getAvailableHealth(), 0);
	EXPECT_FALSE(corpse->alive());
	EXPECT_FALSE(victim->isGhost());
	EXPECT_EQ(victim->getAvailableHealth(), victimHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);
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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000 - cost);
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
	setTestSpellPointTotal(attackerSideHero, 1000);

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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_NE(active->getPosition(), origin);
	EXPECT_EQ(distant->getPosition(), distantPosition);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000 - cost);
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
	setTestSpellPointTotal(attackerSideHero, 1000);
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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);
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
	setTestSpellPointTotal(attackerSideHero, 1000);
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

	const auto manaBefore = attackerSideHero->getManaAvailable();
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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed, temporalFieldBefore);
	EXPECT_EQ(enemyA->getAvailableHealth(), enemyAHealthBefore);
	EXPECT_EQ(enemyB->getAvailableHealth(), enemyBHealthBefore);
	const auto slowEffect = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW)));
	EXPECT_FALSE(enemyA->hasBonus(slowEffect));
	EXPECT_FALSE(enemyB->hasBonus(slowEffect));
	EXPECT_FALSE(enemyC->hasBonus(slowEffect));
	EXPECT_FALSE(enemyD->hasBonus(slowEffect));
}

TEST_F(NewHorizonsMagicAITest, CureAISelectsAndSubmitsAValidAfflictionThroughHypotheticalCastEvaluation)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto knownSpell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(knownSpell);
	attackerSideHero->addSpellToSpellbook(SpellID::CURE);
	const auto lightMagic = SecondarySkill::decode("new-horizons:lightMagic");
	ASSERT_GE(lightMagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * wounded = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(wounded, nullptr);
	ASSERT_NE(enemy, nullptr);

	// Disease's attack/defense group can change this valuable stack's projected
	// combat output, unlike a tiny amount of healing spread across a ten-creature
	// Archangel stack facing an effectively harmless lone enemy.
	for(const auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE})
	{
		auto disease = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL,
			BonusSource::SPELL_EFFECT, -2, BonusSourceID(SpellID(SpellID::DISEASE)), BonusSubtypeID(skill));
		disease->turnsRemain = 3;
		wounded->addNewBonus(disease);
	}
	auto state = wounded->acquireState();
	int64_t damage = 175;
	state->damage(damage);
	BattleUnitsChanged injury;
	injury.battleID = BattleID(0);
	injury.changedStacks.emplace_back(wounded->unitId(), UnitChanges::EOperation::UPDATE);
	injury.changedStacks.back().data = state->save();
	injury.changedStacks.back().healthDelta = -damage;
	gameHandler->sendAndApply(injury);
	ASSERT_EQ(wounded->getCount(), 1);
	ASSERT_GT(wounded->getAvailableHealth(), 0);
	ASSERT_LE(wounded->getAvailableHealth() * 2, wounded->getTotalHealth());
	ASSERT_TRUE(wounded->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::DISEASE)))));

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

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	const auto healthBeforeEvaluation = wounded->getAvailableHealth();
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active))
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	ASSERT_EQ(callback->submitted.size(), 1u)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, SpellID::CURE);
	EXPECT_EQ(action.spellCureAffliction, SpellID::DISEASE);
	const auto selected = action.getTarget(battle());
	ASSERT_EQ(selected.size(), 1u);
	EXPECT_EQ(selected.front().unitValue, wounded);
	const auto healthBeforeCast = wounded->getAvailableHealth();
	const auto manaBeforeCast = attackerSideHero->getManaAvailable();
	EXPECT_EQ(healthBeforeCast, healthBeforeEvaluation);
	EXPECT_TRUE(wounded->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::DISEASE)))));
	EXPECT_EQ(manaBeforeCast, 1000);

	// The simulated choice must be a valid wire action, and the authoritative
	// server must remove exactly the selected Disease source group.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCast - 4);
	EXPECT_GT(wounded->getAvailableHealth(), healthBeforeCast);
	EXPECT_EQ(wounded->getCount(), 1);
	EXPECT_FALSE(wounded->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::DISEASE)))));
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
	setTestSpellPointTotal(attackerSideHero, 1000);
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

TEST_F(NewHorizonsMagicAITest, TransfigureMatterAIChoosesPhysicalObstacleAndKeepsLiveBattleUntouched)
{
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);

	const auto transfigure = transfigureMatterSpell();
	ASSERT_GE(transfigure.getNum(), 0);
	const auto * spell = transfigure.toSpell();
	ASSERT_NE(spell, nullptr);
	EXPECT_EQ(spell->getJsonKey(), transfigureMatterKey);
	attackerSideHero->addSpellToSpellbook(transfigure);
	const auto sorcery = SecondarySkill::decode("new-horizons:sorceryMagic");
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:sorceryMagic", matterShaperPerk});
	setTestSpellPointTotal(attackerSideHero, 1000);

	const BattleHex physicalPosition(8, 5);
	auto physical = std::make_shared<CObstacleInstance>();
	physical->uniqueID = 20;
	physical->ID = 0;
	physical->pos = physicalPosition;
	physical->obstacleType = CObstacleInstance::USUAL;
	battle()->obstacles.push_back(physical);

	auto magical = std::make_shared<SpellCreatedObstacle>();
	magical->uniqueID = 21;
	magical->pos = BattleHex(10, 5);
	magical->customSize.insert(magical->pos);
	battle()->obstacles.push_back(magical);

	auto moat = std::make_shared<CObstacleInstance>();
	moat->uniqueID = 22;
	moat->obstacleType = CObstacleInstance::MOAT;
	moat->pos = BattleHex(12, 5);
	battle()->obstacles.push_back(moat);

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(14, 5), 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != active && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	spells::BattleCast liveCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&liveCast);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	ASSERT_EQ(targets.front().size(), 1u);
	EXPECT_EQ(targets.front().front().hexValue, physicalPosition);

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

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto obstacleCountBefore = battle()->obstacles.size();
	const auto unitCountBefore = battle()->battleGetAllUnits(false).size();
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	{
		// The exact same cast preview the evaluator consumes must contain the
		// temporary Diamond Golems and remove only the selected physical obstacle.
		auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		spells::BattleCast cast(projected.get(), attackerSideHero, spells::Mode::HERO, spell);
		cast.castEval(projected->getServerCallback(), targets.front());
		const auto golems = projected->getUnitsIf([](const battle::Unit * unit)
		{
			return unit->unitType() && unit->unitType()->getJsonKey() == "core:diamondGolem";
		});
		ASSERT_FALSE(golems.empty());
		int64_t golemHealth = 0;
		for(const auto * golem : golems)
			golemHealth += golem->getAvailableHealth();
		EXPECT_GT(golemHealth, 0);
		const auto basePool = 80LL + 2LL * attackerSideHero->getEffectPower(spell)
			+ 50LL * static_cast<int64_t>(physical->getAffectedTiles().size());
		EXPECT_GE(golemHealth, basePool * 125 / 100);
		EXPECT_EQ(projected->getAllObstacles().size(), obstacleCountBefore - 1);
		EXPECT_EQ(battle()->obstacles.size(), obstacleCountBefore);
		EXPECT_EQ(battle()->battleGetAllUnits(false).size(), unitCountBefore);
	}
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.spell, transfigure);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().hexValue, physicalPosition);

	// castEval is a private hypothetical branch of the evaluator. Neither its
	// temporary golems nor obstacle removal may leak to the authoritative battle.
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->obstacles.size(), obstacleCountBefore);
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), unitCountBefore);
	EXPECT_EQ(battle()->battleGetAllObstaclesOnPos(physicalPosition, false).size(), 1u);
}

TEST_F(NewHorizonsMagicAITest, RealEvaluatorUsesInstalledSavedHavocRankAndCost)
{
	// No magic-setting fixture override: actual curated module activation must
	// supply these rules at ordinary new-game initialization.
	prepareCommands(true);
	ASSERT_EQ(gameState()->getMagicRules()["rulesetVersion"].Integer(), 2);
	ASSERT_EQ(gameState()->getMagicRules()["spells"].Struct().size(), 70u);
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
	setTestSpellPointTotal(attackerSideHero, 1000);
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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000 - cost);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
}

TEST_F(NewHorizonsMagicAITest, CanonicalLevelOneHavocRankingCrossesOverWithoutMutatingLiveBattle)
{
	useCurrentMagicRules = true;
	neutralizeCommandEffects = true;
	prepareCommands(true);
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto initialSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto id : initialSpells)
		attackerSideHero->removeSpellFromSpellbook(id);
	const SpellID iceBolt(SpellID::ICE_BOLT);
	const SpellID lightningBolt(SpellID::LIGHTNING_BOLT);
	attackerSideHero->addSpellToSpellbook(iceBolt);
	attackerSideHero->addSpellToSpellbook(lightningBolt);
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 1, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	const auto healthBefore = enemy->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto castsBefore = battle()->battleCastSpells(BattleSide::ATTACKER);
	const auto chooseAtPower = [&](int32_t power)
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		callback->submitted.clear();
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		EXPECT_TRUE(evaluator.attemptCastingSpell(active));
		EXPECT_EQ(callback->submitted.size(), 1u)
			<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
		return callback->submitted.empty() ? SpellID::NONE : callback->submitted.front().spell;
	};

	EXPECT_EQ(chooseAtPower(20), iceBolt)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(enemy->getAvailableHealth(), healthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(chooseAtPower(100), lightningBolt)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(enemy->getAvailableHealth(), healthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), castsBefore);
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
}

TEST_F(NewHorizonsMagicAITest, CanonicalFireballIsPreferredForClusterWithoutMutatingLiveBattle)
{
	useCurrentMagicRules = true;
	neutralizeCommandEffects = true;
	prepareCommands(true);
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto initialSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto id : initialSpells)
		attackerSideHero->removeSpellFromSpellbook(id);
	const SpellID fireball(SpellID::FIREBALL);
	const SpellID iceBolt(SpellID::ICE_BOLT);
	attackerSideHero->addSpellToSpellbook(fireball);
	attackerSideHero->addSpellToSpellbook(iceBolt);
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * first = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(75), 100);
	auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(76), 100);
	ASSERT_TRUE(vstd::contains(BattleHexArray::getNeighbouringTiles(first->getPosition()), second->getPosition()));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	const auto firstHealth = first->getAvailableHealth();
	const auto secondHealth = second->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto castsBefore = battle()->battleCastSpells(BattleSide::ATTACKER);
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(callback->submitted.front().spell, fireball)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(first->getAvailableHealth(), firstHealth);
	EXPECT_EQ(second->getAvailableHealth(), secondHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), castsBefore);
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
}

TEST_F(NewHorizonsMagicAITest, CanonicalFrostRingUsesSafeFriendlyCenter)
{
	useCurrentMagicRules = true;
	neutralizeCommandEffects = true;
	prepareCommands(true);
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto initialSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto id : initialSpells)
		attackerSideHero->removeSpellFromSpellbook(id);
	const SpellID frostRing(SpellID::FROST_RING);
	attackerSideHero->addSpellToSpellbook(frostRing);
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * center = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(75), 1);
	auto * first = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(74), 1);
	auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(76), 1);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	const auto centerHealth = center->getAvailableHealth();
	const auto firstHealth = first->getAvailableHealth();
	const auto secondHealth = second->getAvailableHealth();
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(callback->submitted.front().spell, frostRing)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	ASSERT_EQ(callback->submitted.front().target.size(), 1u)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(callback->submitted.front().target.front().hexValue, center->getPosition());
	EXPECT_EQ(center->getAvailableHealth(), centerHealth);
	EXPECT_EQ(first->getAvailableHealth(), firstHealth);
	EXPECT_EQ(second->getAvailableHealth(), secondHealth);
}

TEST_F(NewHorizonsMagicAITest, CanonicalInfernoIsPreferredForBroadEnemyCluster)
{
	useCurrentMagicRules = true;
	neutralizeCommandEffects = true;
	prepareCommands(true);
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto initialSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto id : initialSpells)
		attackerSideHero->removeSpellFromSpellbook(id);
	const SpellID inferno(SpellID::INFERNO);
	const SpellID iceBolt(SpellID::ICE_BOLT);
	attackerSideHero->addSpellToSpellbook(inferno);
	attackerSideHero->addSpellToSpellbook(iceBolt);
	attackerSideHero->setSecSkillLevel(
		SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 2, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * first = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(75), 100);
	auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(76), 100);
	auto * third = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(77), 100);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	const std::array healthBefore{first->getAvailableHealth(), second->getAvailableHealth(), third->getAvailableHealth()};
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(callback->submitted.front().spell, inferno)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(first->getAvailableHealth(), healthBefore[0]);
	EXPECT_EQ(second->getAvailableHealth(), healthBefore[1]);
	EXPECT_EQ(third->getAvailableHealth(), healthBefore[2]);
}
