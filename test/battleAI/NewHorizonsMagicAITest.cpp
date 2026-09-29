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
#include "../hero/NewHorizonsHeroRulesFixture.h"
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
	explicit MagicCallback(PlayerColor player = PlayerColor(0)) : CBattleCallback(player, nullptr) {}
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
constexpr auto stormOfDaggersKey = "new-horizons:stormOfDaggers";

SpellID transfigureMatterSpell()
{
	return SpellID(SpellID::decode(transfigureMatterKey));
}

SpellID stormOfDaggersSpell()
{
	return SpellID(SpellID::decode(stormOfDaggersKey));
}

void setMorale(CStack * stack, int value)
{
	if(!stack)
		return;
	const int difference = value - stack->moraleVal();
	if(difference == 0)
		return;
	stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MORALE, BonusSource::OTHER, difference, BonusSourceID()));
}

const Bonus * sorrowMoraleBonus(const battle::Unit * unit)
{
	if(!unit)
		return nullptr;
	const auto bonuses = unit->getBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SORROW)))
		.And(Selector::type()(BonusType::MORALE)));
	return bonuses && !bonuses->empty() ? bonuses->front().get() : nullptr;
}

JsonNode legacyMagicRules(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
	}
	if(version < newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		// Saved v1/v2 Sorrow was a Chaos spell with stock level/cost. Preserve
		// that roster snapshot rather than deriving it from the new v3 row.
		auto & sorrow = rules["spells"]["core:sorrow"];
		sorrow.Struct().erase("level");
		sorrow.Struct().erase("costs");
		sorrow["schools"].Vector().clear();
		sorrow["schools"].Vector().emplace_back(std::string("new-horizons:chaos"));
	}

	if(version == newHorizonsMagic::RULESET_VERSION)
	{
		rules.Struct().erase("spellPoints");
		rules.Struct().erase("mageGuildGeneration");
		rules.Struct().erase("physicalDamageReductionCapPercent");
		rules.Struct().erase("warcasting");
		auto & spells = rules["spells"].Struct();
		for(auto it = spells.begin(); it != spells.end();)
		{
			if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
				it = spells.erase(it);
			else
				++it;
		}
		for(auto & [factionId, faction] : rules["factions"].Struct())
		{
			(void)factionId;
			faction["major"] = faction["preferredA"];
			faction["minor"] = faction["preferredB"];
			faction.Struct().erase("preferredA");
			faction.Struct().erase("preferredB");
		}
		for(auto & [name, spell] : rules["spells"].Struct())
		{
			(void)name;
			spell.Struct().erase("active");
			spell.Struct().erase("directDamage");
			spell.Struct().erase("cureAfflictions");
		}
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}
}

class NewHorizonsMagicAITest : public HeroCommandFixture
{
protected:
	bool useCurrentMagicRules = false;
	bool useLegacyMagicRules = false;
	bool useRealHeroScale = false;
	bool neutralizeCommandEffects = false;
	bool useSavedPerkRules = false;
	bool useFocusMagic = false;
	int savedMagicRulesVersion = 0;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(useLegacyMagicRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		else if(savedMagicRulesVersion > 0)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, legacyMagicRules(savedMagicRulesVersion));
		else if(useCurrentMagicRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
				JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		if(useRealHeroScale)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		if(useFocusMagic)
		{
			JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
			ASSERT_FALSE(rules["spells"][newHorizonsSorcery::FOCUS_MAGIC_SPELL].isNull());
			newHorizonsMagic::validateRules(rules);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		}
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

class NewHorizonsDenseMagicAITest : public NewHorizonsMagicAITest, public ::testing::WithParamInterface<bool>
{
};

class NewHorizonsLegacySorrowAITest : public NewHorizonsMagicAITest,
	public ::testing::WithParamInterface<int>
{
};

TEST_F(NewHorizonsMagicAITest, FocusMagicAIValuesSuccessiveShotsAndSelectsFriendlyShooter)
{
	useCommands = false;
	useFocusMagic = true;
	ASSERT_NO_FATAL_FAILURE(startGame());
	const SpellID spell(SpellID::decode(newHorizonsSorcery::FOCUS_MAGIC_SPELL));
	ASSERT_NE(spell, SpellID::NONE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto initialSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto known : initialSpells)
		attackerSideHero->removeSpellFromSpellbook(known);
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 5), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = shooter->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto healthBefore = enemy->getAvailableHealth();
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, shooter, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(shooter);
	ASSERT_TRUE(evaluator.attemptCastingSpell(shooter));
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, spell);
	const auto target = callback->submitted.front().getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	ASSERT_NE(target.front().unitValue, nullptr);
	EXPECT_EQ(target.front().unitValue->unitId(), shooter->unitId());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), healthBefore);
	EXPECT_FALSE(shooter->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))));
}

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

TEST_F(NewHorizonsMagicAITest, SpellLockCastEvaluationMatchesRealMechanicsAcrossTargetShapes)
{
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	for(const auto spell : {SpellID::HASTE, SpellID::SLOW, SpellID::DISPEL, SpellID::MAGIC_ARROW,
		SpellID::FIREBALL, SpellID::TELEPORT})
		attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	auto * lockedAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * openAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 10);
	auto * lockedEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
	auto * openEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 10);
	ASSERT_NE(lockedAlly, nullptr);
	ASSERT_NE(openAlly, nullptr);
	ASSERT_NE(lockedEnemy, nullptr);
	ASSERT_NE(openEnemy, nullptr);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != lockedAlly && unit != openAlly && unit != lockedEnemy && unit != openEnemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	ASSERT_NE(spellLock, SpellID::NONE);
	const auto applyLock = [&](CStack * unit)
	{
		auto lock = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
			BonusSource::SPELL_EFFECT, 100, BonusSourceID(spellLock));
		lock->turnsRemain = 3;
		unit->addNewBonus(lock);
	};
	const auto addSpellEffect = [](CStack * unit, SpellID source, int32_t value)
	{
		auto effect = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, value, BonusSourceID(source));
		effect->turnsRemain = 3;
		unit->addNewBonus(effect);
	};
	applyLock(lockedAlly);
	applyLock(lockedEnemy);
	addSpellEffect(lockedAlly, SpellID(SpellID::HASTE), 3);
	addSpellEffect(openAlly, SpellID(SpellID::HASTE), 3);
	addSpellEffect(lockedEnemy, SpellID(SpellID::SLOW), -3);
	addSpellEffect(openEnemy, SpellID(SpellID::SLOW), -3);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	const std::array<const CStack *, 4> units{lockedAlly, openAlly, lockedEnemy, openEnemy};
	const auto statusSnapshot = [](const battle::Unit * unit, SpellID source)
	{
		std::vector<std::pair<int32_t, int32_t>> result;
		const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(source)));
		if(bonuses)
			for(const auto & bonus : *bonuses)
				if(bonus && Bonus::NTurns(bonus.get()))
					result.emplace_back(bonus->val, bonus->turnsRemain);
		return result;
	};
	const auto runParity = [&](SpellID spellId, const spells::Target & liveAim, bool castable)
	{
		const auto * spell = spellId.toSpell();
		ASSERT_NE(spell, nullptr);
		HypotheticBattle model(environment.get(), callback->getBattle(BattleID(0)));
		spells::Target projectedAim;
		for(const auto & destination : liveAim)
		{
			if(destination.unitValue)
			{
				const auto * projected = model.battleGetUnitByID(destination.unitValue->unitId());
				ASSERT_NE(projected, nullptr);
				projectedAim.emplace_back(projected, destination.hexValue);
			}
			else
				projectedAim.emplace_back(destination.hexValue);
		}
		spells::BattleCast simulated(&model, attackerSideHero, spells::Mode::HERO, spell);
		simulated.setSpellLevel(3);
		EXPECT_EQ(spell->battleMechanics(&simulated)->canBeCastAt(projectedAim), castable)
			<< spellId.getNum();
		simulated.castEval(model.getServerCallback(), projectedAim);

		spells::BattleCast authoritative(battle(), attackerSideHero, spells::Mode::HERO, spell);
		authoritative.setSpellLevel(3);
		authoritative.cast(gameHandler->spellcastEnvironment(), liveAim);
		for(const auto * liveUnit : units)
		{
			const auto * projected = model.battleGetUnitByID(liveUnit->unitId());
			ASSERT_NE(projected, nullptr);
			EXPECT_EQ(projected->getAvailableHealth(), liveUnit->getAvailableHealth()) << spellId.getNum();
			EXPECT_EQ(projected->getCount(), liveUnit->getCount()) << spellId.getNum();
			EXPECT_EQ(projected->getPosition(), liveUnit->getPosition()) << spellId.getNum();
			for(const auto status : {SpellID(SpellID::HASTE), SpellID(SpellID::SLOW)})
				EXPECT_EQ(statusSnapshot(projected, status), statusSnapshot(liveUnit, status)) << spellId.getNum();
		}
	};

	const spells::Target lockedAllyAim{spells::Destination(lockedAlly)};
	const spells::Target openAllyAim{spells::Destination(openAlly)};
	const spells::Target lockedEnemyAim{spells::Destination(lockedEnemy)};
	const spells::Target openEnemyAim{spells::Destination(openEnemy)};
	runParity(SpellID::HASTE, lockedAllyAim, false);
	EXPECT_EQ(statusSnapshot(lockedAlly, SpellID(SpellID::HASTE)).size(), 1u);
	runParity(SpellID::HASTE, openAllyAim, true);
	runParity(SpellID::SLOW, lockedEnemyAim, false);
	EXPECT_EQ(statusSnapshot(lockedEnemy, SpellID(SpellID::SLOW)).size(), 1u);
	runParity(SpellID::SLOW, openEnemyAim, true);
	runParity(SpellID::MAGIC_ARROW, lockedEnemyAim, false);
	runParity(SpellID::MAGIC_ARROW, openEnemyAim, true);
	EXPECT_LT(openEnemy->getAvailableHealth(), lockedEnemy->getAvailableHealth());
	runParity(SpellID::DISPEL, lockedAllyAim, false);
	EXPECT_EQ(statusSnapshot(lockedAlly, SpellID(SpellID::HASTE)).size(), 1u);
	runParity(SpellID::DISPEL, openAllyAim, true);
	EXPECT_TRUE(statusSnapshot(openAlly, SpellID(SpellID::HASTE)).empty());

	const auto accessibility = battle()->getAccessibility(openAlly);
	BattleHex teleportDestination = BattleHex::INVALID;
	for(si16 offset = 1; offset < GameConstants::BFIELD_SIZE; ++offset)
	{
		const BattleHex candidate(static_cast<si16>(openAlly->getPosition().toInt() + offset));
		if(candidate.isAvailable() && accessibility.accessible(candidate, openAlly))
		{
			teleportDestination = candidate;
			break;
		}
	}
	ASSERT_TRUE(teleportDestination.isAvailable());
	const auto lockedAllyPosition = lockedAlly->getPosition();
	const spells::Target lockedTeleportAim{spells::Destination(lockedAlly),
		spells::Destination(teleportDestination)};
	runParity(SpellID::TELEPORT, lockedTeleportAim, false);
	EXPECT_EQ(lockedAlly->getPosition(), lockedAllyPosition);
	const spells::Target openTeleportAim{spells::Destination(openAlly),
		spells::Destination(teleportDestination)};
	runParity(SpellID::TELEPORT, openTeleportAim, true);
	EXPECT_EQ(openAlly->getPosition(), teleportDestination);

	// Fireball's location target covers both neighboring enemy stacks: the
	// locked one remains protected while the unmarked one takes damage.
	const auto lockedEnemyHealth = lockedEnemy->getAvailableHealth();
	const auto openEnemyHealth = openEnemy->getAvailableHealth();
	runParity(SpellID::FIREBALL,
		{spells::Destination(lockedEnemy->getPosition())}, true);
	EXPECT_EQ(lockedEnemy->getAvailableHealth(), lockedEnemyHealth);
	EXPECT_LT(openEnemy->getAvailableHealth(), openEnemyHealth);
}

TEST_F(NewHorizonsMagicAITest, SpellLockAIUsesSavedRankSpellbinderAndEchoedDuration)
{
	useCurrentMagicRules = true;
	useSavedPerkRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	ASSERT_NE(spellLock, SpellID::NONE);
	attackerSideHero->addSpellToSpellbook(spellLock);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
	ASSERT_NE(target, nullptr);
	auto haste = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::HASTE)));
	haste->turnsRemain = 3;
	target->addNewBonus(haste);

	const auto score = [&](bool followup)
	{
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spellLock.toSpell());
		event.setSpellLevel(3);
		event.setMetamagicFollowup(followup);
		if(followup)
			event.setMetamagicTargetUnitId(target->unitId());
		const auto mechanics = spellLock.toSpell()->battleMechanics(&event);
		return SpellTargetEvaluator::spellLockPlacementValue(
			mechanics.get(), spells::Target{spells::Destination(target)});
	};

	const auto sorcery = SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL);
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 70, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), MasteryLevel::NONE,
		ChangeValueMode::ABSOLUTE);
	const float noRankValue = score(false);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	const float basicRankValue = score(false);
	EXPECT_GT(basicRankValue, noRankValue)
		<< "The saved Basic coefficient moves Spell Power 70 across the first duration threshold";

	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 160, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({newHorizonsSorcery::SORCERY_MAGIC_SKILL,
		newHorizonsSorcery::SPELLBINDER_PERK});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::SPELLBINDER_PERK));
	const float spellbinderValue = score(false);

	const auto metamagic = SecondarySkill::decode("new-horizons:metamagic");
	ASSERT_GE(metamagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(metamagic), MasteryLevel::ADVANCED,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic",
		"new-horizons:metamagic.arcaneAcquisition"});
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic",
		std::string(newHorizonsMagic::METAMAGIC_ECHOED_DURATION)});
	ASSERT_TRUE(newHorizonsMagic::hasMetamagicPerk(
		attackerSideHero, newHorizonsMagic::METAMAGIC_ECHOED_DURATION));
	const float echoedValue = score(true);

	EXPECT_GT(spellbinderValue, basicRankValue);
	EXPECT_GT(echoedValue, spellbinderValue)
		<< "Echoed Duration is applied after the Spellbinder 4-round cap";
}

TEST_F(NewHorizonsMagicAITest, SpellLockDoesNotBlockNonmagicalCreatureAbilityInRealOrForecast)
{
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto * ability = SpellID(SpellID::decode("core:fireballAbility")).toSpell();
	ASSERT_NE(ability, nullptr);
	ASSERT_TRUE(ability->isCreatureAbility());
	ASSERT_FALSE(ability->isMagical());

	auto * caster = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 2), 10);
	auto * lockedEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 100);
	auto * openEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(13, 5), 100);
	ASSERT_NE(caster, nullptr);
	ASSERT_NE(lockedEnemy, nullptr);
	ASSERT_NE(openEnemy, nullptr);
	Bonus creaturePower;
	creaturePower.type = BonusType::CREATURE_SPELL_POWER;
	creaturePower.val = 100;
	caster->addNewBonus(std::make_shared<Bonus>(creaturePower));

	const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	auto lock = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
		BonusSource::SPELL_EFFECT, 100, BonusSourceID(spellLock));
	lock->turnsRemain = 3;
	lockedEnemy->addNewBonus(lock);

	const spells::Target aim{spells::Destination(lockedEnemy->getPosition())};
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	HypotheticBattle model(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast forecast(&model, caster, spells::Mode::CREATURE_ACTIVE, ability);
	const auto forecastMechanics = ability->battleMechanics(&forecast);
	ASSERT_TRUE(forecastMechanics->canBeCastAt(aim));
	const auto * forecastLocked = model.battleGetUnitByID(lockedEnemy->unitId());
	const auto * forecastOpen = model.battleGetUnitByID(openEnemy->unitId());
	ASSERT_NE(forecastLocked, nullptr);
	ASSERT_NE(forecastOpen, nullptr);
	const auto lockedHealth = lockedEnemy->getAvailableHealth();
	const auto openHealth = openEnemy->getAvailableHealth();
	forecastMechanics->castEval(model.getServerCallback(), aim);
	EXPECT_LT(forecastLocked->getAvailableHealth(), lockedHealth);
	EXPECT_LT(forecastOpen->getAvailableHealth(), openHealth);

	spells::BattleCast authoritative(battle(), caster, spells::Mode::CREATURE_ACTIVE, ability);
	const auto authoritativeMechanics = ability->battleMechanics(&authoritative);
	ASSERT_TRUE(authoritativeMechanics->canBeCastAt(aim));
	authoritativeMechanics->cast(gameHandler->spellcastEnvironment(), aim);
	EXPECT_EQ(forecastLocked->getAvailableHealth(), lockedEnemy->getAvailableHealth());
	EXPECT_EQ(forecastOpen->getAvailableHealth(), openEnemy->getAvailableHealth());
	EXPECT_LT(lockedEnemy->getAvailableHealth(), lockedHealth);
	EXPECT_LT(openEnemy->getAvailableHealth(), openHealth);
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

TEST_P(NewHorizonsDenseMagicAITest, DenseBattleEvaluationWithSavedPerksIsStableAndReadOnly)
{
	useCommands = GetParam();
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

	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto centaur = creatureByName("core:centaur");
	const auto dwarf = creatureByName("core:dwarf");
	const auto pikeman = creatureByName("core:pikeman");
	const std::array<CStack *, 10> stacks{
		addStack(BattleSide::ATTACKER, centaur, BattleHex(2, 1), 3),
		addStack(BattleSide::ATTACKER, dwarf, BattleHex(3, 3), 3),
		addStack(BattleSide::ATTACKER, centaur, BattleHex(2, 5), 3),
		addStack(BattleSide::ATTACKER, dwarf, BattleHex(3, 7), 3),
		addStack(BattleSide::ATTACKER, centaur, BattleHex(4, 9), 3),
		addStack(BattleSide::ATTACKER, dwarf, BattleHex(5, 2), 3),
		addStack(BattleSide::ATTACKER, centaur, BattleHex(5, 6), 3),
		addStack(BattleSide::DEFENDER, pikeman, BattleHex(13, 1), 14),
		addStack(BattleSide::DEFENDER, pikeman, BattleHex(14, 5), 15),
		addStack(BattleSide::DEFENDER, pikeman, BattleHex(13, 9), 14),
	};
	for(const auto * stack : stacks)
	{
		ASSERT_NE(stack, nullptr);
	}
	auto * active = stacks[8];
	ASSERT_EQ(active->unitSide(), BattleSide::DEFENDER);
	ASSERT_EQ(active->getCount(), 15);
	ASSERT_EQ(battle()->battleGetOwnerHero(active), defenderSideHero);

	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(std::ranges::find(stacks, unit) == stacks.end())
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	ASSERT_EQ(battle()->battleGetAllUnits(false).size(), stacks.size());
	std::set<int32_t> occupiedHexes;
	for(const auto * stack : stacks)
	{
		ASSERT_TRUE(stack->getPosition().isValid());
		for(const auto & hex : stack->getHexes())
		{
			EXPECT_TRUE(hex.isAvailable()) << "unavailable occupied battle hex " << hex;
			EXPECT_TRUE(occupiedHexes.insert(hex.toInt()).second)
				<< "overlapping occupied battle hex " << hex;
		}
	}

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), active->unitId());

	auto snapshotBattleUnits = [&]()
	{
		std::vector<std::tuple<uint32_t, int32_t, int32_t, int64_t>> snapshot;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			snapshot.emplace_back(unit->unitId(), unit->getPosition().toInt(),
				unit->getCount(), unit->getAvailableHealth());
		std::ranges::sort(snapshot);
		return snapshot;
	};
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto actionIdentity = [](const BattleAction & action)
	{
		std::vector<std::pair<int32_t, int32_t>> targetIdentity;
		targetIdentity.reserve(action.target.size());
		for(const auto & target : action.target)
			targetIdentity.emplace_back(target.unitValue, target.hexValue.toInt());
		return std::tuple{
			static_cast<int32_t>(action.side), action.stackNumber, static_cast<int32_t>(action.actionType),
			action.spell.getNum(), action.spellOvercharge, action.spellSelectiveDispel,
			action.spellCureAffliction.getNum(), action.spellMassSlow, action.perfectMoment,
			static_cast<int32_t>(action.spellFireWallDirection), action.metamagicFollowup,
			action.metamagicGrand, action.metamagicDecline, action.timeStopHeroActionPass,
			action.metamagicManaRefund,
			static_cast<int32_t>(action.command), action.gatingCreature.getNum(), std::move(targetIdentity)};
	};
	auto evaluateFormation = [&](const std::string & propertyPrefix)
	{
		const auto stateBefore = gameState()->saveToMemory();
		const auto roundBefore = battle()->battleGetRound();
		const auto activeUnitBefore = battle()->battleActiveUnit()->unitId();
		const auto battleUnitsBefore = snapshotBattleUnits();

		constexpr int sampleCount = 3;
		std::array<BattleAction, sampleCount> actions;
		std::array<int64_t, sampleCount> sampleMicros{};
		for(int sample = 0; sample < sampleCount; ++sample)
		{
			auto callback = std::make_shared<MagicCallback>(PlayerColor(1));
			callback->onBattleStarted(battle());
			const auto started = std::chrono::steady_clock::now();
			BattleEvaluator evaluator(environment, callback, active, PlayerColor(1), BattleID(0),
				BattleSide::DEFENDER, 1.0f, 2);
			actions[sample] = evaluator.selectStackAction(active);
			const auto finished = std::chrono::steady_clock::now();
			sampleMicros[sample] = std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count();
		}

		const auto firstActionIdentity = actionIdentity(actions.front());
		for(int sample = 1; sample < sampleCount; ++sample)
			EXPECT_EQ(actionIdentity(actions[sample]), firstActionIdentity);
		RecordProperty(propertyPrefix + "ActionSignature", testing::PrintToString(firstActionIdentity));
		EXPECT_EQ(actions.front().side, BattleSide::DEFENDER);
		EXPECT_EQ(actions.front().stackNumber, active->unitId());

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
		RecordProperty(propertyPrefix + "StackCount", std::to_string(stacks.size()));
		RecordProperty(propertyPrefix + "TargetStackCount", "7");
		RecordProperty(propertyPrefix + "SampleCount", std::to_string(sampleCount));
		RecordProperty(propertyPrefix + "SampleMicros", sampleTimes.str());
		RecordProperty(propertyPrefix + "WaitedBeforeEvaluation", active->waitedThisTurn ? "true" : "false");
	};

	evaluateFormation("denseOpening");

	const std::array<BattleHex, 10> crowdedPositions{
		BattleHex(7, 2), BattleHex(8, 2), BattleHex(7, 4), BattleHex(8, 4),
		BattleHex(7, 6), BattleHex(8, 6), BattleHex(7, 8),
		BattleHex(13, 2), BattleHex(14, 5), BattleHex(13, 8),
	};
	for(size_t index = 0; index < stacks.size(); ++index)
	{
		BattleStackMoved move;
		move.battleID = BattleID(0);
		move.stack = stacks[index]->unitId();
		move.teleporting = true;
		move.tilesToMove.insert(crowdedPositions[index]);
		gameHandler->sendAndApply(move);
	}
	std::set<int32_t> crowdedOccupiedHexes;
	for(const auto * stack : stacks)
		for(const auto & hex : stack->getHexes())
		{
			ASSERT_TRUE(hex.isAvailable());
			ASSERT_TRUE(crowdedOccupiedHexes.insert(hex.toInt()).second)
				<< "overlapping crowded battle hex " << hex;
		}

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeWait(active)));
	ASSERT_TRUE(active->waitedThisTurn);
	BattleSetActiveStack reactivate;
	reactivate.battleID = BattleID(0);
	reactivate.stack = active->unitId();
	reactivate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(reactivate);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), active->unitId());
	ASSERT_TRUE(active->waitedThisTurn);
	evaluateFormation("denseCrowded");
}

INSTANTIATE_TEST_SUITE_P(CommandRules, NewHorizonsDenseMagicAITest, ::testing::Bool(),
	[](const ::testing::TestParamInfo<bool> & info)
	{
		return info.param ? "NHCommands" : "LegacyCommands";
	});

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

TEST_F(NewHorizonsMagicAITest, MetamagicAIFirstSequenceDoesNotRequestGrand)
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

TEST_F(NewHorizonsMagicAITest, MetamagicAIThirdUsedSequenceSubmitsSpellWithoutManualGrandFlag)
{
	useCommands = true;
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
	metamagicState.metamagicUsesConsumed = 2;
	// Mirror the already accepted base cast in the typed action ledger too.
	auto & ledger = metamagicState.heroActionAllowances;
	const auto base = ledger.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound());
	ASSERT_TRUE(base);
	ASSERT_TRUE(ledger.consumeAllowance(base->grantId, HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound()));
	ledger.grantAllowance(HeroActionAllowanceState::AllowanceKind::SPELL,
		HeroActionAllowanceState::GrantSource::METAMAGIC, battle()->getRound());

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
	// The callback only records the request. AI evaluation must not spend the
	// real allowance or mark Grand used before the server accepts the cast.
	EXPECT_EQ(metamagicState.metamagicUsesConsumed, 2);
	EXPECT_FALSE(metamagicState.metamagicGrandUsed);
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

TEST_F(NewHorizonsMagicAITest, V3MagicArrowAIProjectionAndAuthoritativeCastUseSchoolRankCoefficient)
{
	useCommands = true;
	neutralizeCommandEffects = true;
	useCurrentMagicRules = true;
	useRealHeroScale = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	const auto sorcery = SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic"));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
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

	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(arrow, nullptr);
	ASSERT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(battle()->getMagicRules(), attackerSideHero,
		SpellID::MAGIC_ARROW), 145);
	const auto expectedRaw = newHorizonsMagic::magicArrowDamage(battle()->getMagicRules(), SpellID::MAGIC_ARROW,
		attackerSideHero->getEffectPower(arrow), attackerSideHero->getEffectPowerDivisor(arrow), 0,
		newHorizonsMagic::magicArrowOverchargeModifiers(attackerSideHero), 145);
	ASSERT_TRUE(expectedRaw.has_value());

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	const auto healthBefore = enemy->getAvailableHealth();
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast preview(projected.get(), attackerSideHero, spells::Mode::HERO, arrow);
	spells::Target previewTarget;
	previewTarget.emplace_back(projected->battleGetUnitByID(enemy->unitId()));
	preview.castEval(projected->getServerCallback(), previewTarget);
	EXPECT_EQ(healthBefore - projected->battleGetUnitByID(enemy->unitId())->getAvailableHealth(), *expectedRaw);
	EXPECT_EQ(enemy->getAvailableHealth(), healthBefore);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	EXPECT_EQ(action.spell, SpellID::MAGIC_ARROW);
	const auto actionTarget = action.getTarget(battle());
	ASSERT_EQ(actionTarget.size(), 1u);
	ASSERT_NE(actionTarget.front().unitValue, nullptr);
	EXPECT_EQ(actionTarget.front().unitValue->unitId(), enemy->unitId());
	auto chosenProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast chosenPreview(chosenProjection.get(), attackerSideHero, spells::Mode::HERO, arrow);
	chosenPreview.setOvercharge(action.spellOvercharge);
	spells::Target chosenTarget;
	chosenTarget.emplace_back(chosenProjection->battleGetUnitByID(enemy->unitId()));
	chosenPreview.castEval(chosenProjection->getServerCallback(), chosenTarget);
	const auto projectedDamage = healthBefore
		- chosenProjection->battleGetUnitByID(enemy->unitId())->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(healthBefore - enemy->getAvailableHealth(), projectedDamage)
		<< "Battle AI projection and authoritative cast must share the v3 ranked formula";
}

TEST_F(NewHorizonsMagicAITest, V3NaturePoisonAIValuesRankScaledMarginalTicksAgainstLegalEnemies)
{
	useCommands = true;
	neutralizeCommandEffects = true;
	useCurrentMagicRules = true;
	useRealHeroScale = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));

	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto known : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(known);
	const SpellID poison(SpellID::decode(std::string(newHorizonsMagic::NATURE_POISON_SPELL)));
	ASSERT_NE(poison, SpellID::NONE);
	ASSERT_NE(poison.toSpell(), nullptr);
	attackerSideHero->addSpellToSpellbook(poison);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	ASSERT_TRUE(newHorizonsMagic::physicalPoisonEnabled(battle()->getMagicRules(), poison));

	const SecondarySkill nature(SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL)));
	ASSERT_NE(nature, SecondarySkill(SecondarySkill::NONE));
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 100);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(enemy, nullptr);

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

	std::vector<float> rankValues;
	for(const auto rank : {MasteryLevel::NONE, MasteryLevel::BASIC,
		MasteryLevel::ADVANCED, MasteryLevel::EXPERT})
	{
		attackerSideHero->setSecSkillLevel(nature, rank, ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, poison.toSpell());
		auto mechanics = poison.toSpell()->battleMechanics(&cast);
		const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
		ASSERT_EQ(targets.size(), 1u);
		ASSERT_EQ(targets.front().size(), 1u);
		EXPECT_EQ(targets.front().front().unitValue, enemy);
		rankValues.push_back(SpellTargetEvaluator::naturePoisonPlacementValue(mechanics.get(), targets.front()));
		EXPECT_EQ(enemy->physicalPoisonBaseDamage, 0);
		EXPECT_EQ(enemy->physicalPoisonActivationsRemaining, 0);
	}
	ASSERT_EQ(rankValues.size(), 4u);
	EXPECT_GT(rankValues[0], 0.0f);
	EXPECT_GT(rankValues[1], rankValues[0]);
	EXPECT_GT(rankValues[2], rankValues[1]);
	EXPECT_GT(rankValues[3], rankValues[2]);

	// A weaker cast must not receive value while a stronger physical affliction
	// remains active. The read-only evaluator preserves the live status fields.
	enemy->physicalPoisonBaseDamage = 1000;
	enemy->physicalPoisonActivationsRemaining = 3;
	enemy->physicalPoisonSourceStackId = -1;
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, poison.toSpell());
	auto mechanics = poison.toSpell()->battleMechanics(&cast);
	const spells::Target enemyTarget{spells::Destination(enemy)};
	EXPECT_EQ(SpellTargetEvaluator::naturePoisonPlacementValue(mechanics.get(), enemyTarget), 0.0f);
	EXPECT_EQ(enemy->physicalPoisonBaseDamage, 1000);
	EXPECT_EQ(enemy->physicalPoisonActivationsRemaining, 3);

	// Once that affliction expires, the complete BattleAI path recognizes the
	// spell, selects the legal enemy, and leaves live battle state untouched.
	enemy->physicalPoisonBaseDamage = 0;
	enemy->physicalPoisonActivationsRemaining = 0;
	enemy->physicalPoisonSourceStackId = -1;
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.spell, poison);
	const auto selectedTarget = action.getTarget(battle());
	ASSERT_EQ(selectedTarget.size(), 1u);
	EXPECT_EQ(selectedTarget.front().unitValue, enemy);
	EXPECT_EQ(enemy->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(enemy->physicalPoisonActivationsRemaining, 0);
}

TEST_F(NewHorizonsMagicAITest, V3BlessHypotheticalForecastMatchesTheSingleTargetAuthoritativeCast)
{
	useCommands = true;
	useCurrentMagicRules = true;
	useRealHeroScale = true;
	useSavedPerkRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	const auto initialSpells = attackerSideHero->getSpellsInSpellbook();
	for(const SpellID known : initialSpells)
		attackerSideHero->removeSpellFromSpellbook(known);
	const auto * bless = SpellID(SpellID::BLESS).toSpell();
	ASSERT_NE(bless, nullptr);
	attackerSideHero->addSpellToSpellbook(bless->getId());
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 160, ChangeValueMode::ABSOLUTE);
	const auto light = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.benediction"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:lightMagic", "new-horizons:lightMagic.benediction"));
	setTestSpellPointTotal(attackerSideHero, 1000);

	auto * selected = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * otherFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(6, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	ASSERT_NE(selected, nullptr);
	ASSERT_NE(otherFriendly, nullptr);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto * projectedSelected = projected->battleGetUnitByID(selected->unitId());
	auto * projectedOther = projected->battleGetUnitByID(otherFriendly->unitId());
	ASSERT_NE(projectedSelected, nullptr);
	ASSERT_NE(projectedOther, nullptr);
	spells::BattleCast preview(projected.get(), attackerSideHero, spells::Mode::HERO, bless);
	const auto mechanics = bless->battleMechanics(&preview);
	EXPECT_EQ(mechanics->getRangeLevel(), 2);
	EXPECT_FALSE(mechanics->isMassive());
	const spells::Target previewTarget{spells::Destination(projectedSelected)};
	ASSERT_TRUE(mechanics->canBeCastAt(previewTarget));
	preview.castEval(projected->getServerCallback(), previewTarget);
	const auto projectedBless = projected->getForUpdate(selected->unitId())->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS))));
	ASSERT_FALSE(projectedBless->empty());
	int predictedDuration = 0;
	std::optional<int32_t> predictedEndpointValue;
	for(const auto & bonus : *projectedBless)
		if(bonus->type == BonusType::ALWAYS_MAXIMUM_DAMAGE)
		{
			predictedDuration = bonus->turnsRemain;
			predictedEndpointValue = bonus->val;
		}
	EXPECT_EQ(predictedDuration, 5);
	EXPECT_EQ(predictedEndpointValue, 0)
		<< "the v3 hypothetical cast uses the saved endpoint-only Bless effect";
	EXPECT_TRUE(projected->getForUpdate(otherFriendly->unitId())->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS))))->empty());

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::BLESS;
	action.aimToUnit(selected);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto actualBless = selected->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS))));
	ASSERT_FALSE(actualBless->empty());
	int actualDuration = 0;
	std::optional<int32_t> actualEndpointValue;
	for(const auto & bonus : *actualBless)
		if(bonus->type == BonusType::ALWAYS_MAXIMUM_DAMAGE)
		{
			actualDuration = bonus->turnsRemain;
			actualEndpointValue = bonus->val;
		}
	EXPECT_EQ(actualDuration, predictedDuration)
		<< "AI hypothetical projection and authoritative Bless cast must agree";
	EXPECT_EQ(actualEndpointValue, predictedEndpointValue)
		<< "AI hypothetical projection and authoritative Bless cast must agree on the endpoint value";
	EXPECT_TRUE(otherFriendly->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS))))->empty())
		<< "Expert School does not convert the single-target spell into Mass Bless";
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

TEST_F(NewHorizonsMagicAITest, QuicksandAISelectsExactLegalGroundAndValuesHostileApproach)
{
	useCommands = false;
	useCurrentMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	attackerSideHero->addSpellToSpellbook(SpellID::QUICKSAND);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 60, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != ally && unit != enemy)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	const auto * spell = SpellID(SpellID::QUICKSAND).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(newHorizonsMagic::quicksandSelectedPlacementEnabled(
		battle()->getMagicRules(), spell->getId()));
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	ASSERT_EQ(targets.front().size(), static_cast<size_t>(mechanics->getNewHorizonsQuicksandPatchCount()));
	std::set<int> selected;
	for(const auto & destination : targets.front())
	{
		ASSERT_EQ(destination.unitValue, nullptr);
		EXPECT_TRUE(selected.insert(destination.hexValue.toInt()).second);
		EXPECT_TRUE(newHorizonsMagic::quicksandPlacementHexIsLegal(*battle(), destination.hexValue));
	}
	EXPECT_TRUE(mechanics->canBeCastAt(targets.front()));
	EXPECT_GT(SpellTargetEvaluator::quicksandPlacementValue(mechanics.get(), targets.front()), 0.0f);
	const auto first = targets.front();
	const auto second = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(second.size(), 1u);
	for(size_t index = 0; index < first.size(); ++index)
		EXPECT_EQ(first[index].hexValue, second.front()[index].hexValue);

	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -static_cast<int32_t>(ally->getMovementRange());
	ally->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = ally->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto recordingCallback = std::make_shared<MagicCallback>();
	recordingCallback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, recordingCallback, ally, PlayerColor(0),
		BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(ally);
	ASSERT_TRUE(evaluator.attemptCastingSpell(ally));
	ASSERT_EQ(recordingCallback->submitted.size(), 1u);
	EXPECT_EQ(recordingCallback->submitted.front().spell, SpellID::QUICKSAND);
	EXPECT_EQ(recordingCallback->submitted.front().target.size(), first.size());
}

TEST_F(NewHorizonsMagicAITest, QuicksandAIKeepsLegacyNoTargetSelection)
{
	useCommands = false;
	useLegacyMagicRules = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	attackerSideHero->addSpellToSpellbook(SpellID::QUICKSAND);
	setTestSpellPointTotal(attackerSideHero, 1000);

	const auto * spell = SpellID(SpellID::QUICKSAND).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_FALSE(newHorizonsMagic::quicksandSelectedPlacementEnabled(
		battle()->getMagicRules(), spell->getId()));
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	EXPECT_TRUE(targets.front().empty());
	EXPECT_EQ(SpellTargetEvaluator::quicksandPlacementValue(mechanics.get(), targets.front()), 0.0f);
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

TEST_F(NewHorizonsMagicAITest, HerbalistRegenerationAIValuesProjectedWoundsWithoutHealingAtCastTime)
{
	useCurrentMagicRules = true;
	useSavedPerkRules = true;
	neutralizeCommandEffects = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));

	const SpellID regeneration(SpellID::decode(std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL)));
	ASSERT_NE(regeneration, SpellID::NONE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto knownSpell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(knownSpell);
	attackerSideHero->addSpellToSpellbook(regeneration);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const auto natureMagic = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagic), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL),
		std::string(newHorizonsMagic::NATURE_HERBALIST)});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL),
		std::string(newHorizonsMagic::NATURE_HERBALIST)));
	setTestSpellPointTotal(attackerSideHero, 1000);

	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
	auto * selected = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(7, 5), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(8, 5), 10);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(selected, nullptr);
	ASSERT_NE(enemy, nullptr);

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

	const auto healthBeforeEvaluation = selected->getAvailableHealth();
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active))
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	ASSERT_EQ(callback->submitted.size(), 1u)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	const auto action = callback->submitted.front();
	EXPECT_EQ(action.spell, regeneration);
	const auto selectedTarget = action.getTarget(battle());
	ASSERT_EQ(selectedTarget.size(), 1u);
	EXPECT_EQ(selectedTarget.front().unitValue, selected);
	EXPECT_EQ(selected->getAvailableHealth(), healthBeforeEvaluation)
		<< "AI valuation must project future wounds; evaluating the spell cannot heal live battle state";
	EXPECT_EQ(selected->regenerationRateMillionths, 0);
	EXPECT_FALSE(selected->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(regeneration))));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(selected->getAvailableHealth(), healthBeforeEvaluation)
		<< "Regeneration marks damage for the next activation instead of curing current wounds";
	EXPECT_EQ(selected->regenerationRateMillionths, 350'000)
		<< "Basic Herbalist adds ten percentage points to the 25% base rate at zero Spell Power";
	EXPECT_TRUE(selected->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(regeneration))));
}

TEST_F(NewHorizonsMagicAITest, RegenerationAIDoesNotTreatExistingWoundsAsImmediateHealing)
{
	useCurrentMagicRules = true;
	neutralizeCommandEffects = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));

	const SpellID regeneration(SpellID::decode(std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL)));
	ASSERT_NE(regeneration, SpellID::NONE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto knownSpell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(knownSpell);
	attackerSideHero->addSpellToSpellbook(regeneration);
	setTestSpellPointTotal(attackerSideHero, 1000);

	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
	auto * wounded = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(7, 5), 1);
	auto * distantEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(13, 5), 1);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(wounded, nullptr);
	ASSERT_NE(distantEnemy, nullptr);

	auto woundState = wounded->acquireState();
	int64_t damage = 40;
	woundState->damage(damage);
	BattleUnitsChanged injury;
	injury.battleID = BattleID(0);
	injury.changedStacks.emplace_back(wounded->unitId(), UnitChanges::EOperation::UPDATE);
	injury.changedStacks.back().data = woundState->save();
	injury.changedStacks.back().healthDelta = -damage;
	gameHandler->sendAndApply(injury);

	for(auto * stack : {active, distantEnemy})
	{
		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -stack->getMovementRange();
		stack->addNewBonus(std::make_shared<Bonus>(immobilized));
	}
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
	EXPECT_FALSE(evaluator.attemptCastingSpell(active))
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_EQ(wounded->getAvailableHealth(), wounded->getMaxHealth() - damage);
	EXPECT_EQ(wounded->regenerationRateMillionths, 0);
}

TEST_F(NewHorizonsMagicAITest, RegenerationForecastConsumesMarksOnceAndSkipsDeniedActivations)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());

	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto * wounded = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(7, 5), 1);
	ASSERT_NE(wounded, nullptr);

	const SpellID regeneration(SpellID::decode(std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL)));
	auto state = wounded->acquireState();
	int64_t damage = 80;
	state->damage(damage);
	state->regenerationRateMillionths = 500'000;
	state->regenerationPendingMicroHealth = 40'000'000;
	BattleUnitsChanged injury;
	injury.battleID = BattleID(0);
	injury.changedStacks.emplace_back(wounded->unitId(), UnitChanges::EOperation::UPDATE);
	injury.changedStacks.back().data = state->save();
	injury.changedStacks.back().healthDelta = -damage;
	gameHandler->sendAndApply(injury);

	Bonus marker(BonusDuration::N_TURNS, BonusType::HP_REGENERATION,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(regeneration));
	marker.turnsRemain = 3;
	wounded->addNewBonus(std::make_shared<Bonus>(marker));

	const auto liveHealth = wounded->getAvailableHealth();
	const auto livePending = wounded->regenerationPendingMicroHealth;
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto forecast = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto forecastStack = forecast->getForUpdate(wounded->unitId());
	const auto initialForecastHealth = forecastStack->getAvailableHealth();
	ASSERT_EQ(forecastStack->regenerationProjectedHeal(), 40);

	forecast->nextTurn(wounded->unitId(), BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_EQ(forecastStack->getAvailableHealth(), initialForecastHealth);
	EXPECT_EQ(forecastStack->regenerationPendingMicroHealth, livePending);
	EXPECT_TRUE(forecast->battleBeginsActivation(forecastStack.get(), BattleUnitTurnReason::TURN_QUEUE));

	forecast->nextTurn(wounded->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(forecastStack->getAvailableHealth(), initialForecastHealth + 40);
	EXPECT_EQ(forecastStack->regenerationPendingMicroHealth, 0);
	forecast->nextTurn(wounded->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(forecastStack->getAvailableHealth(), initialForecastHealth + 40)
		<< "consumed marks must not be valued or applied again on later projected activations";

	auto stoppedForecast = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	Bonus timeStop(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 0, BonusSourceID(regeneration));
	stoppedForecast->addUnitBonus(wounded->unitId(), {timeStop});
	auto stoppedStack = stoppedForecast->getForUpdate(wounded->unitId());
	ASSERT_TRUE(stoppedStack->isTimeStopped());
	EXPECT_FALSE(stoppedForecast->battleBeginsActivation(stoppedStack.get(), BattleUnitTurnReason::TURN_QUEUE));
	stoppedForecast->nextTurn(wounded->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(stoppedStack->getAvailableHealth(), liveHealth);
	EXPECT_EQ(stoppedStack->regenerationPendingMicroHealth, livePending);

	EXPECT_EQ(wounded->getAvailableHealth(), liveHealth);
	EXPECT_EQ(wounded->regenerationPendingMicroHealth, livePending)
		<< "projected activation processing must not mutate authoritative battle state";
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
	ASSERT_EQ(gameState()->getMagicRules()["rulesetVersion"].Integer(), 3);
	ASSERT_EQ(gameState()->getMagicRules()["spells"].Struct().size(), 72u);
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
	ASSERT_EQ(spell->calculateDamage(attackerSideHero), 11066);
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

TEST_F(NewHorizonsMagicAITest, StormOfDaggersAISelectsFiveDistinctEnemyStacksWithoutFriendlyFire)
{
	useCommands = true;
	neutralizeCommandEffects = true;
	useCurrentMagicRules = true;
	useRealHeroScale = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));

	const auto storm = stormOfDaggersSpell();
	ASSERT_NE(storm, SpellID::NONE);
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(storm);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(5, 5), 10);
	std::vector<CStack *> enemies;
	for(int index = 0; index < 21; ++index)
		enemies.push_back(addStack(BattleSide::DEFENDER, creatureByName("core:ogre"),
			BattleHex(9 + index % 8, 2 + index / 8), 100));
	ASSERT_NE(active, nullptr);
	ASSERT_NE(friendly, nullptr);
	ASSERT_EQ(enemies.size(), 21u);
	ASSERT_TRUE(std::all_of(enemies.begin(), enemies.end(), [](const CStack * unit) { return unit != nullptr; }));

	std::set<uint32_t> keepIds{active->unitId(), friendly->unitId()};
	for(const auto * enemy : enemies)
		keepIds.insert(enemy->unitId());
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(!keepIds.contains(unit->unitId()))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	// Spell immunity makes the target illegal while leaving its ordinary damage
	// forecast positive, so it would consume a best-N slot if legality were only
	// checked after ranking.
	enemies.front()->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(storm)));
	ASSERT_TRUE(enemies.front()->hasImmunity(storm));
	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -active->getMovementRange();
	active->addNewBonus(std::make_shared<Bonus>(immobilized));

	spells::BattleCast selectionProbe(battle(), attackerSideHero, spells::Mode::HERO, storm.toSpell());
	const auto selectionMechanics = storm.toSpell()->battleMechanics(&selectionProbe);
	ASSERT_TRUE(selectionMechanics->setStormOfDaggersTargetCount(1));
	ASSERT_GT(selectionMechanics->adjustEffectValue(enemies.front()), 0);
	ASSERT_EQ(enemies.front()->magicResistance(), 0);
	const spells::Target invincibleTarget{spells::Destination(enemies.front())};
	ASSERT_FALSE(selectionMechanics->canBeCastAt(invincibleTarget));
	const auto legalSubsets = SpellTargetEvaluator::getViableTargets(selectionMechanics.get());
	ASSERT_EQ(legalSubsets.size(), 5u)
		<< "The full 21-stack target set is bounded to one candidate per legal target count, "
		<< "even when the highest-ranked immune unit is illegal";
	std::array<size_t, 6> subsetsBySize{};
	for(const auto & subset : legalSubsets)
	{
		ASSERT_GE(subset.size(), 1u);
		ASSERT_LE(subset.size(), 5u);
		++subsetsBySize[subset.size()];
		std::set<uint32_t> subsetIds;
		for(const auto & destination : subset)
		{
			ASSERT_NE(destination.unitValue, nullptr);
			EXPECT_NE(destination.unitValue, friendly);
			EXPECT_NE(destination.unitValue, enemies.front())
				<< "A high-ranked immune target must not suppress legal alternatives";
			EXPECT_EQ(battle()->battleGetOwner(destination.unitValue), PlayerColor(1));
			EXPECT_TRUE(subsetIds.insert(destination.unitValue->unitId()).second);
		}
	}
	EXPECT_EQ((std::array<size_t, 6>{0, 1, 1, 1, 1, 1}), subsetsBySize);

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, active,
		PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	EXPECT_EQ(action.spell, storm);
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 5u);
	std::set<uint32_t> selected;
	for(const auto & destination : target)
	{
		ASSERT_NE(destination.unitValue, nullptr);
		EXPECT_NE(destination.unitValue, friendly);
		EXPECT_EQ(battle()->battleGetOwner(destination.unitValue), PlayerColor(1));
		EXPECT_TRUE(selected.insert(destination.unitValue->unitId()).second);
	}
	for(const auto * enemy : enemies)
		EXPECT_EQ(battle()->battleGetOwner(enemy), PlayerColor(1));
}

TEST_F(NewHorizonsMagicAITest, StormOfDaggersAIValuesSavedSchoolRankAndMagicResistance)
{
	useCommands = true;
	neutralizeCommandEffects = true;
	useCurrentMagicRules = true;
	useRealHeroScale = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));

	const auto storm = stormOfDaggersSpell();
	ASSERT_NE(storm, SpellID::NONE);
	const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
	for(const auto spell : knownSpells)
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(storm);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	const auto sorcery = SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL);
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * susceptible = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 3), 100);
	auto * resistant = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 7), 100);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(susceptible, nullptr);
	ASSERT_NE(resistant, nullptr);
	resistant->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	ASSERT_EQ(resistant->magicResistance(), 100);

	const std::set<uint32_t> keepIds{active->unitId(), susceptible->unitId(), resistant->unitId()};
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(!keepIds.contains(unit->unitId()))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -active->getMovementRange();
	active->addNewBonus(std::make_shared<Bonus>(immobilized));

	const auto * spell = storm.toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast liveCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto liveMechanics = spell->battleMechanics(&liveCast);
	ASSERT_TRUE(liveMechanics->isNewHorizonsStormOfDaggers());
	EXPECT_EQ(liveMechanics->getSchoolRankPowerCoefficientPercent(), 145);
	EXPECT_EQ(liveMechanics->getStormOfDaggersTotalDamage(1), 263);
	EXPECT_EQ(liveMechanics->getStormOfDaggersDamagePerTarget(1), 263);
	EXPECT_EQ(liveMechanics->getStormOfDaggersTotalDamage(2), 302);
	EXPECT_EQ(liveMechanics->getStormOfDaggersDamagePerTarget(2), 151);

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	const auto susceptibleHealth = susceptible->getAvailableHealth();
	const auto resistantHealth = resistant->getAvailableHealth();
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	EXPECT_EQ(action.spell, storm)
		<< describeMagicAIState(*callback, battle()->getMagicRules(), battle()->getHeroCommandRules());
	const auto actionTarget = action.getTarget(battle());
	ASSERT_EQ(actionTarget.size(), 1u);
	ASSERT_NE(actionTarget.front().unitValue, nullptr);
	EXPECT_EQ(actionTarget.front().unitValue->unitId(), susceptible->unitId())
		<< "The two-target cast loses value because its second stack resists with certainty";

	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto * projectedSusceptible = projected->battleGetUnitByID(susceptible->unitId());
	ASSERT_NE(projectedSusceptible, nullptr);
	spells::BattleCast preview(projected.get(), attackerSideHero, spells::Mode::HERO, spell);
	auto previewMechanics = spell->battleMechanics(&preview);
	ASSERT_TRUE(previewMechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(actionTarget.size())));
	const spells::Target previewTarget{spells::Destination(projectedSusceptible)};
	ASSERT_TRUE(previewMechanics->canBeCastAt(previewTarget));
	previewMechanics->castEval(projected->getServerCallback(), previewTarget);
	const auto projectedDamage = susceptibleHealth
		- projected->battleGetUnitByID(susceptible->unitId())->getAvailableHealth();
	EXPECT_EQ(projectedDamage, liveMechanics->getStormOfDaggersDamagePerTarget(1));
	EXPECT_EQ(susceptible->getAvailableHealth(), susceptibleHealth);
	EXPECT_EQ(resistant->getAvailableHealth(), resistantHealth);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(susceptibleHealth - susceptible->getAvailableHealth(), projectedDamage)
		<< "The BattleAI forecast and authoritative cast must share the saved-v3 per-target amount";
	EXPECT_EQ(resistant->getAvailableHealth(), resistantHealth);
}

TEST_F(NewHorizonsMagicAITest, CanonicalShadowSorrowAIUsesProjectedRankedMoraleToRankLegalTargets)
{
	useCommands = true;
	neutralizeCommandEffects = true;
	useCurrentMagicRules = true;
	useRealHeroScale = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));

	const SpellID sorrow(SpellID::SORROW);
	const auto shadowSkillId = SecondarySkill::decode("new-horizons:shadowMagic");
	ASSERT_GE(shadowSkillId, 0);
	const SecondarySkill shadowMagic(shadowSkillId);
	attackerSideHero->addSpellToSpellbook(sorrow);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
	ASSERT_NE(active, nullptr);
	const auto adjacentHexes = BattleHexArray::getNeighbouringTiles(active->getPosition());
	ASSERT_GE(adjacentHexes.size(), 2u);
	auto adjacent = adjacentHexes.begin();
	auto * highMoraleThreat = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), *adjacent++, 19);
	auto * lowMoraleThreat = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), *adjacent, 12);
	ASSERT_NE(highMoraleThreat, nullptr);
	ASSERT_NE(lowMoraleThreat, nullptr);
	setMorale(highMoraleThreat, 3);
	setMorale(lowMoraleThreat, 1);

	const std::set<uint32_t> keepIds{active->unitId(), highMoraleThreat->unitId(), lowMoraleThreat->unitId()};
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(!keepIds.contains(unit->unitId()))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	ASSERT_EQ(highMoraleThreat->moraleVal(), 3);
	ASSERT_EQ(lowMoraleThreat->moraleVal(), 1);
	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	const auto highMoraleBefore = highMoraleThreat->moraleVal();
	const auto lowMoraleBefore = lowMoraleThreat->moraleVal();

	// Spell mechanics apply saved-rules School rank in each hypothetical cast;
	// score each legal projection with the production AI helper to assert its
	// rank-sensitive target ordering. End-to-end action selection against the
	// ordinary active-stack baseline remains deferred to Phase 2.
	const std::array<uint32_t, 2> targetIds{highMoraleThreat->unitId(), lowMoraleThreat->unitId()};
	std::array<std::array<float, 2>, 2> targetScores{};
	std::array<std::array<int, 2>, 2> projectedMorale{};
	std::array<std::array<int, 2>, 2> projectedPenalty{};
	std::array<int32_t, 2> effectPowerByRank{};
	std::array<int32_t, 2> effectPowerDivisorByRank{};
	for(const auto [rankIndex, rank] : {std::pair{0, MasteryLevel::NONE}, std::pair{1, MasteryLevel::EXPERT}})
	{
		attackerSideHero->setSecSkillLevel(shadowMagic, rank, ChangeValueMode::ABSOLUTE);
		for(size_t targetIndex = 0; targetIndex < targetIds.size(); ++targetIndex)
		{
			auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
			const auto * castTarget = projected->battleGetUnitByID(targetIds[targetIndex]);
			ASSERT_NE(castTarget, nullptr);
			spells::BattleCast preview(projected.get(), attackerSideHero, spells::Mode::HERO, sorrow.toSpell());
			auto mechanics = sorrow.toSpell()->battleMechanics(&preview);
			ASSERT_FALSE(mechanics->isMassive());
			effectPowerByRank[rankIndex] = mechanics->getEffectPower();
			effectPowerDivisorByRank[rankIndex] = mechanics->getEffectPowerDivisor();
			const spells::Target aim{spells::Destination(castTarget)};
			ASSERT_TRUE(mechanics->canBeCastAt(aim));
			mechanics->castEval(projected->getServerCallback(), aim);
			const auto * projectedTarget = projected->battleGetUnitByID(targetIds[targetIndex]);
			ASSERT_NE(projectedTarget, nullptr);
			projectedMorale[rankIndex][targetIndex] = projectedTarget->moraleVal();
			const auto * effect = sorrowMoraleBonus(projectedTarget);
			ASSERT_NE(effect, nullptr);
			EXPECT_EQ(effect->turnsRemain, 3);
			projectedPenalty[rankIndex][targetIndex] = -effect->val;

			DamageCache projectedDamage;
			projectedDamage.buildDamageCache(projected, BattleSide::ATTACKER);
			const auto * originalTarget = battle()->battleGetUnitByID(targetIds[targetIndex]);
			ASSERT_NE(originalTarget, nullptr);
			targetScores[rankIndex][targetIndex] = BattleEvaluator::estimateProjectedSorrowTargetValue(
				originalTarget, projectedTarget, projectedDamage, projected);
		}
	}
	EXPECT_EQ(effectPowerByRank[0], 100);
	EXPECT_EQ(effectPowerByRank[0], effectPowerByRank[1]);
	EXPECT_GT(effectPowerDivisorByRank[0], 0);
	EXPECT_EQ(effectPowerDivisorByRank[0], effectPowerDivisorByRank[1]);
	EXPECT_EQ(projectedPenalty[0][0], 2)
		<< "raw effect power " << effectPowerByRank[0] << ", legacy divisor " << effectPowerDivisorByRank[0];
	EXPECT_EQ(projectedPenalty[1][0], 3)
		<< "raw effect power " << effectPowerByRank[1] << ", legacy divisor " << effectPowerDivisorByRank[1];
	EXPECT_EQ(projectedPenalty[0][1], 2);
	EXPECT_EQ(projectedPenalty[1][1], 3);
	EXPECT_EQ(projectedMorale[0][0], 1);
	EXPECT_EQ(projectedMorale[1][0], 0)
		<< "At 100 raw Spell Power, saved-v3 Expert Shadow crosses the next penalty threshold";
	EXPECT_EQ(projectedMorale[0][1], -1);
	EXPECT_EQ(projectedMorale[1][1], -2);

	spells::BattleCast liveCast(battle(), attackerSideHero, spells::Mode::HERO, sorrow.toSpell());
	const auto liveMechanics = sorrow.toSpell()->battleMechanics(&liveCast);
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(liveMechanics.get());
	ASSERT_EQ(viableTargets.size(), 2u);
	for(const auto & target : viableTargets)
	{
		ASSERT_EQ(target.size(), 1u);
		ASSERT_NE(target.front().unitValue, nullptr);
		EXPECT_TRUE(target.front().unitValue->unitId() == highMoraleThreat->unitId()
			|| target.front().unitValue->unitId() == lowMoraleThreat->unitId());
	}

	// At no rank the larger +3-Morale stack has the higher expected activation
	// loss. Expert rank pushes the +1-Morale stack farther into negative Morale,
	// making its projected Sorrow score higher and flipping the legal-target pick.
	EXPECT_GT(targetScores[0][0], targetScores[0][1]);
	EXPECT_GT(targetScores[1][1], targetScores[1][0]);

	EXPECT_EQ(highMoraleThreat->moraleVal(), highMoraleBefore);
	EXPECT_EQ(lowMoraleThreat->moraleVal(), lowMoraleBefore);
	EXPECT_EQ(sorrowMoraleBonus(highMoraleThreat), nullptr);
	EXPECT_EQ(sorrowMoraleBonus(lowMoraleThreat), nullptr);
}

TEST_P(NewHorizonsLegacySorrowAITest, SavedV1AndV2AIStillValuesLegacyMassSorrow)
{
	useCommands = true;
	neutralizeCommandEffects = true;
	useRealHeroScale = true;
	savedMagicRulesVersion = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), GetParam());

	const SpellID sorrow(SpellID::SORROW);
	const auto chaosSkillId = SecondarySkill::decode("new-horizons:chaosMagic");
	ASSERT_GE(chaosSkillId, 0);
	const SecondarySkill chaosMagic(chaosSkillId);
	attackerSideHero->addSpellToSpellbook(sorrow);
	attackerSideHero->setSecSkillLevel(chaosMagic, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
	auto * firstEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 3), 10);
	auto * secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(12, 7), 10);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(firstEnemy, nullptr);
	ASSERT_NE(secondEnemy, nullptr);
	setMorale(firstEnemy, 1);
	setMorale(secondEnemy, 1);

	const std::set<uint32_t> keepIds{active->unitId(), firstEnemy->unitId(), secondEnemy->unitId()};
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(!keepIds.contains(unit->unitId()))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	const auto * spell = sorrow.toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast liveCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto liveMechanics = spell->battleMechanics(&liveCast);
	EXPECT_TRUE(liveMechanics->isMassive());
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(liveMechanics.get());
	ASSERT_EQ(viableTargets.size(), 1u);
	EXPECT_TRUE(viableTargets.front().empty());

	auto callback = std::make_shared<MagicCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<MagicEnvironment>(gameState());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto firstMoraleBefore = firstEnemy->moraleVal();
	const auto secondMoraleBefore = secondEnemy->moraleVal();
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast preview(projected.get(), attackerSideHero, spells::Mode::HERO, spell);
	auto projectedMechanics = spell->battleMechanics(&preview);
	const auto projectedTargets = SpellTargetEvaluator::getViableTargets(projectedMechanics.get());
	ASSERT_EQ(projectedTargets.size(), 1u);
	projectedMechanics->castEval(projected->getServerCallback(), projectedTargets.front());
	const auto * projectedFirst = projected->battleGetUnitByID(firstEnemy->unitId());
	const auto * projectedSecond = projected->battleGetUnitByID(secondEnemy->unitId());
	ASSERT_NE(projectedFirst, nullptr);
	ASSERT_NE(projectedSecond, nullptr);
	ASSERT_NE(sorrowMoraleBonus(projectedFirst), nullptr);
	ASSERT_NE(sorrowMoraleBonus(projectedSecond), nullptr);
	EXPECT_LT(projectedFirst->moraleVal(), firstEnemy->moraleVal());
	EXPECT_LT(projectedSecond->moraleVal(), secondEnemy->moraleVal());

	// Legacy v1/v2 still use their saved mass-target effect. Score each applied
	// target with the same production AI value helper used by candidate scoring.
	DamageCache projectedDamage;
	projectedDamage.buildDamageCache(projected, BattleSide::ATTACKER);
	const float legacyMassSorrowValue = BattleEvaluator::estimateProjectedSorrowTargetValue(
		firstEnemy, projectedFirst, projectedDamage, projected)
		+ BattleEvaluator::estimateProjectedSorrowTargetValue(secondEnemy, projectedSecond, projectedDamage, projected);
	EXPECT_GT(legacyMassSorrowValue, 0.0f);

	EXPECT_EQ(firstEnemy->moraleVal(), firstMoraleBefore);
	EXPECT_EQ(secondEnemy->moraleVal(), secondMoraleBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(sorrowMoraleBonus(firstEnemy), nullptr);
	EXPECT_EQ(sorrowMoraleBonus(secondEnemy), nullptr);
}

INSTANTIATE_TEST_SUITE_P(SavedProfiles, NewHorizonsLegacySorrowAITest,
	::testing::Values(newHorizonsMagic::RULESET_VERSION,
		newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION));
