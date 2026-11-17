/*
 * NewHorizonsBerserkAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include "../../AI/BattleAI/SearchIncumbent.h"
#include "../PerkTestSetup.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/battle/NewHorizonsBerserk.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <cstddef>
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace
{
constexpr auto CHAOS_MAGIC = "new-horizons:chaosMagic";
constexpr auto FRENZIED_CURSE = "new-horizons:chaosMagic.frenziedCurse";

class BerserkEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit BerserkEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class BerserkCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	explicit BerserkCallback(PlayerColor player = PlayerColor(0)) : CBattleCallback(player, nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

void addBerserkBonus(CStack * unit)
{
	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 1,
		BonusSourceID(SpellID(SpellID::BERSERK))));
}

template<typename T>
std::vector<std::byte> serializedState(T & object)
{
	CMemorySerializer serializer;
	serializer.oser & object;
	return serializer.extractBuffer();
}
}

class NewHorizonsBerserkAITest : public HeroCommandFixture
{
protected:
	// HypotheticBattle borrows its Environment; keep the adapter alive alongside
	// every forecast created by this fixture.
	std::shared_ptr<BerserkEnvironment> forecastEnvironment;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		bool foundFrenziedCurse = false;
		for(auto & perk : perkRules["skills"][CHAOS_MAGIC]["perks"].Vector())
			if(perk["id"].String() == FRENZIED_CURSE)
			{
				perk["effect"]["status"].String() = "active";
				foundFrenziedCurse = true;
			}
		if(!foundFrenziedCurse)
			throw std::runtime_error("Missing Frenzied Curse registry entry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		// Keep this fixture focused on the spell's projected activation value.
		const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
		JsonNode commandRules = combatRules["combat"]["heroCommands"];
		for(auto & commandEntry : commandRules["commands"].Struct())
			for(auto & effectEntry : commandEntry.second["effects"].Struct())
			{
				auto & formula = effectEntry.second;
				formula["base"].Float() = 0;
				formula["attack"].Float() = 0;
				formula["defense"].Float() = 0;
			}
		heroCommands::validateRules(commandRules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void removeInitialStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void startEmptyBattle()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		removeInitialStacks();
	}

	void acceptFrenziedCurse(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < perkTestSetup::seedLimit(hero->getPerkState(), 4096); ++seed)
		{
			const auto offer = perkTestSetup::forSkill(hero->getPerkState(), CHAOS_MAGIC, rankLookup, seed, hero->getPerkOfferWeightContext());
			const auto selected = std::ranges::find_if(offer, [](const auto & candidate)
			{
				return candidate.selection.skillId == CHAOS_MAGIC
					&& candidate.selection.perkId == FRENZIED_CURSE;
			});
			if(selected == offer.end())
				continue;
			gameHandler->levelUpHero(hero, offer,
				static_cast<size_t>(std::distance(offer.begin(), selected)), seed, false);
			ASSERT_TRUE(hero->hasActivePerk(CHAOS_MAGIC, FRENZIED_CURSE));
			return;
		}
		FAIL() << "No legal Basic Frenzied Curse offer";
	}

	void prepareBerserkCaster(bool frenziedCurse = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(SpellID::BERSERK);
		const auto chaosMagic = SecondarySkill::decode(CHAOS_MAGIC);
		ASSERT_GE(chaosMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(chaosMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		if(frenziedCurse)
			ASSERT_NO_FATAL_FAILURE(acceptFrenziedCurse(attackerSideHero));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		removeInitialStacks();
	}

	void prepareActualBerserk(bool frenziedCurse, CStack *& berserker, CStack *& forcedTarget)
	{
		ASSERT_NO_FATAL_FAILURE(prepareBerserkCaster(frenziedCurse));
		auto * castingStack = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(1, 5), 1);
		berserker = addStack(BattleSide::DEFENDER,
			creatureByName("core:archer"), BattleHex(5, 5), 20);
		ASSERT_NE(castingStack, nullptr);
		ASSERT_NE(berserker, nullptr);

		const auto normalMovement = berserker->getMovementRange();
		const auto targetX = BattleHex(5, 5).getX() + static_cast<si16>(normalMovement) + 2;
		ASSERT_LT(targetX, GameConstants::BFIELD_WIDTH - 1);
		forcedTarget = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(targetX, 5), 20);
		ASSERT_NE(forcedTarget, nullptr);

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = castingStack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		BattleAction cast;
		cast.actionType = EActionType::HERO_SPELL;
		cast.side = BattleSide::ATTACKER;
		cast.spell = SpellID::BERSERK;
		cast.aimToUnit(berserker);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast));

		BattleUnitsChanged removeCaster;
		removeCaster.battleID = BattleID(0);
		removeCaster.changedStacks.emplace_back(castingStack->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removeCaster);
	}

	std::shared_ptr<HypotheticBattle> makeForecast()
	{
		forecastEnvironment = std::make_shared<BerserkEnvironment>(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		return std::make_shared<HypotheticBattle>(forecastEnvironment.get(), callback);
	}
};

TEST_F(NewHorizonsBerserkAITest, V3ShooterForecastUsesForcedMeleeAttack)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * berserker = addStack(BattleSide::DEFENDER,
		creatureByName("core:archer"), BattleHex(8, 5), 5);
	auto * target = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(9, 5), 5);
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(target, nullptr);
	addBerserkBonus(berserker);

	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto forecast = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
	const auto forced = forecast->getBerserkForcedActions(projectedBerserker.get());
	ASSERT_FALSE(forced.empty());
	ASSERT_EQ(forced.front().type, EActionType::WALK_AND_ATTACK);
	ASSERT_NE(forced.front().target, nullptr);
	EXPECT_EQ(forced.front().target->unitId(), target->unitId());

	DamageCache damage;
	PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
	ASSERT_FALSE(candidates.possibleAttacks.empty());
	for(const auto & attack : candidates.possibleAttacks)
	{
		EXPECT_FALSE(attack.attack.shooting);
		EXPECT_EQ(attack.attack.defender->unitId(), target->unitId());
		EXPECT_EQ(attack.from, forced.front().position);
	}
}

TEST_F(NewHorizonsBerserkAITest, ForcedWalkKeepsItsDestinationAndNoActionDoesNotReposition)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * berserker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(1, 5), 5);
	auto * distantTarget = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(16, 5), 5);
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(distantTarget, nullptr);
	addBerserkBonus(berserker);

	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto forecast = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
	const auto forced = forecast->getBerserkForcedActions(projectedBerserker.get());
	ASSERT_EQ(forced.size(), 1u);
	ASSERT_EQ(forced.front().type, EActionType::WALK);

	DamageCache damage;
	PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
	BattleExchangeEvaluator evaluator(forecast, environment, 1.0f, 1);
	const auto movement = evaluator.findMoveTowardsUnreachable(
		projectedBerserker.get(), candidates, damage, forecast);
	ASSERT_EQ(movement.positions.size(), 1u);
	EXPECT_TRUE(movement.positions.contains(forced.front().position));
	EXPECT_EQ(candidates.unreachableEnemies.size(), 1u);
	EXPECT_EQ(candidates.unreachableEnemies.front()->unitId(), distantTarget->unitId());

	auto stationaryForecast = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const Bonus immobilized(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -20, BonusSourceID());
	stationaryForecast->addUnitBonus(berserker->unitId(), {immobilized});
	const auto stationary = stationaryForecast->getForUpdate(berserker->unitId());
	const auto stationaryForced = stationaryForecast->getBerserkForcedActions(stationary.get());
	EXPECT_TRUE(stationaryForced.empty());
	DamageCache stationaryDamage;
	PotentialTargets stationaryCandidates(stationary.get(), stationaryDamage, stationaryForecast);
	ASSERT_EQ(stationaryCandidates.forcedBerserkActions.size(), 1u);
	EXPECT_EQ(stationaryCandidates.forcedBerserkActions.front().type, EActionType::NO_ACTION);
	BattleExchangeEvaluator stationaryEvaluator(stationaryForecast, environment, 1.0f, 1);
	const auto stationaryMove = stationaryEvaluator.findMoveTowardsUnreachable(
		stationary.get(), stationaryCandidates, stationaryDamage, stationaryForecast);
	EXPECT_TRUE(stationaryMove.positions.empty());
	EXPECT_TRUE(stationaryCandidates.unreachableEnemies.empty());
}

TEST_F(NewHorizonsBerserkAITest, ExpectedBerserkValueAveragesEveryTiedAttackCandidate)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * berserker = addStack(BattleSide::DEFENDER,
		creatureByName("core:archer"), BattleHex(8, 5), 20);
	auto * hostileVictim = addStack(BattleSide::ATTACKER,
		creatureByName("core:archangel"), BattleHex(7, 5), 1);
	auto * friendlyVictim = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(9, 5), 30);
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(hostileVictim, nullptr);
	ASSERT_NE(friendlyVictim, nullptr);
	addBerserkBonus(berserker);

	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto forecast = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
	const auto forced = forecast->getBerserkForcedActions(projectedBerserker.get());
	ASSERT_EQ(forced.size(), 2u);
	for(const auto & action : forced)
		EXPECT_EQ(action.type, EActionType::WALK_AND_ATTACK);

	DamageCache damage;
	PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
	ASSERT_EQ(candidates.possibleAttacks.size(), forced.size());
	float summedActionValue = 0.0f;
	for(const auto & action : forced)
	{
		const auto attack = std::ranges::find_if(candidates.possibleAttacks, [&](const AttackPossibility & candidate)
		{
			return candidate.attack.defender->unitId() == action.target->unitId()
				&& candidate.from == action.position;
		});
		ASSERT_NE(attack, candidates.possibleAttacks.end());
		summedActionValue += attack->attackValue();
	}
	EXPECT_NEAR(candidates.expectedBerserkActionValue(),
		summedActionValue / static_cast<float>(forced.size()), 0.001f);
	EXPECT_EQ(candidates.bestAction().attack.defender->unitId(), forced.front().target->unitId());
}

TEST_F(NewHorizonsBerserkAITest, FrenziedCurseAddsSpeedOnlyToTheDetachedForcedActivation)
{
	CStack * berserker = nullptr;
	CStack * forcedTarget = nullptr;
	ASSERT_NO_FATAL_FAILURE(prepareActualBerserk(true, berserker, forcedTarget));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(CHAOS_MAGIC, FRENZIED_CURSE));

	const auto * liveBerserker = battle()->battleGetUnitByID(berserker->unitId());
	ASSERT_NE(liveBerserker, nullptr);
	const auto spellMarkers = liveBerserker->getBonuses(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE)
		.And(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BERSERK)))));
	ASSERT_EQ(spellMarkers->size(), 1u);
	EXPECT_EQ(spellMarkers->front()->spellCasterOwner, PlayerColor(0));
	const auto vanillaForced = battle()->getBerserkForcedActions(liveBerserker);
	ASSERT_EQ(vanillaForced.size(), 1u);
	EXPECT_EQ(vanillaForced.front().type, EActionType::WALK);

	auto forecast = makeForecast();
	auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
	const auto callerMovement = projectedBerserker->getMovementRange();
	DamageCache damage;
	PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
	ASSERT_TRUE(candidates.berserk);
	ASSERT_EQ(candidates.forcedBerserkActions.size(), 1u);
	EXPECT_EQ(candidates.forcedBerserkActions.front().type, EActionType::WALK_AND_ATTACK);
	ASSERT_EQ(candidates.forcedBerserkActions.front().target->unitId(), forcedTarget->unitId());
	ASSERT_EQ(candidates.possibleAttacks.size(), 1u);
	EXPECT_GT(candidates.expectedBerserkActionValue(), 0.0f);

	// The selected path used +2 movement, but the returned projected attack and
	// caller's longer-lived state no longer contain the action-scoped Speed.
	EXPECT_EQ(projectedBerserker->getMovementRange(), callerMovement);
	EXPECT_EQ(candidates.possibleAttacks.front().attack.attacker->getMovementRange(), callerMovement);
	const auto remainingSpeed = candidates.possibleAttacks.front().attack.attacker->getBonuses(
		Selector::type()(BonusType::STACKS_SPEED));
	EXPECT_FALSE(std::ranges::any_of(*remainingSpeed, [](const auto & bonus)
	{
		return bonus && newHorizonsBerserk::isFrenziedCurseSpeedBonus(bonus.get());
	}));
}

TEST_F(NewHorizonsBerserkAITest, FrenziedCurseRequiresTheSavedCasterAndDoesNotOverridePuppetControl)
{
	CStack * berserker = nullptr;
	CStack * forcedTarget = nullptr;
	ASSERT_NO_FATAL_FAILURE(prepareActualBerserk(true, berserker, forcedTarget));

	{
		auto forecast = makeForecast();
		auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
		const auto berserkSource = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BERSERK)))
			.And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE));
		const auto markers = projectedBerserker->getBonuses(berserkSource);
		ASSERT_EQ(markers->size(), 1u);
		Bonus wrongCaster = *markers->front();
		wrongCaster.spellCasterOwner = PlayerColor(1);
		projectedBerserker->removeUnitBonus(berserkSource);
		projectedBerserker->addUnitBonus({wrongCaster});

		DamageCache damage;
		PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
		ASSERT_TRUE(candidates.berserk);
		ASSERT_EQ(candidates.forcedBerserkActions.size(), 1u);
		EXPECT_EQ(candidates.forcedBerserkActions.front().type, EActionType::WALK);
	}

	{
		auto forecast = makeForecast();
		auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
		const auto puppetSpell = SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
		ASSERT_TRUE(puppetSpell.hasValue());
		forecast->addUnitBonus(berserker->unitId(),
			{newHorizonsPuppetMaster::controlMarker(puppetSpell, PlayerColor(0))});
		ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*forecast, projectedBerserker.get()));
		EXPECT_FALSE(newHorizonsBerserk::forcedActivationSpeedBonus(*forecast, projectedBerserker.get()));

		DamageCache damage;
		PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
		EXPECT_FALSE(candidates.berserk);
		EXPECT_TRUE(candidates.forcedBerserkActions.empty());
	}
}

TEST_F(NewHorizonsBerserkAITest, BerserkWithoutSelectedFrenziedCurseKeepsNormalForcedMovement)
{
	CStack * berserker = nullptr;
	CStack * forcedTarget = nullptr;
	ASSERT_NO_FATAL_FAILURE(prepareActualBerserk(false, berserker, forcedTarget));
	ASSERT_FALSE(attackerSideHero->hasActivePerk(CHAOS_MAGIC, FRENZIED_CURSE));

	auto forecast = makeForecast();
	auto projectedBerserker = forecast->getForUpdate(berserker->unitId());
	DamageCache damage;
	PotentialTargets candidates(projectedBerserker.get(), damage, forecast);
	ASSERT_TRUE(candidates.berserk);
	ASSERT_EQ(candidates.forcedBerserkActions.size(), 1u);
	EXPECT_EQ(candidates.forcedBerserkActions.front().type, EActionType::WALK);
	EXPECT_EQ(candidates.forcedBerserkActions.front().target->unitId(), forcedTarget->unitId());
	EXPECT_TRUE(candidates.possibleAttacks.empty());
}

TEST_F(NewHorizonsBerserkAITest, CompletedForcedActivationRemovalStaysInDetachedReplayBranch)
{
	CStack * berserker = nullptr;
	CStack * forcedTarget = nullptr;
	ASSERT_NO_FATAL_FAILURE(prepareActualBerserk(true, berserker, forcedTarget));
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(forcedTarget, nullptr);

	const auto otherSpellSource = BonusSourceID(SpellID(SpellID::BLIND));
	berserker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::CREATURE_ABILITY, 0, BonusSourceID()));
	berserker->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
		BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 0, otherSpellSource));

	const auto liveBerserkSource = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BERSERK))).And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE));
	const auto intrinsicSource = Selector::source(BonusSource::CREATURE_ABILITY, BonusSourceID())
		.And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE));
	const auto otherSpellForcedSource = Selector::source(BonusSource::SPELL_EFFECT, otherSpellSource)
		.And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE));
	ASSERT_TRUE(berserker->hasBonus(liveBerserkSource));
	ASSERT_TRUE(berserker->hasBonus(intrinsicSource));
	ASSERT_TRUE(berserker->hasBonus(otherSpellForcedSource));

	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto sourceForecast = makeForecast();
	const auto projected = sourceForecast->getForUpdate(berserker->unitId());
	DamageCache damage;
	PotentialTargets candidates(projected.get(), damage, sourceForecast);
	ASSERT_TRUE(candidates.berserk);
	ASSERT_EQ(candidates.forcedBerserkActions.size(), 1u);
	EXPECT_EQ(candidates.forcedBerserkActions.front().type, EActionType::WALK_AND_ATTACK);
	ASSERT_EQ(candidates.forcedBerserkActions.front().target->unitId(), forcedTarget->unitId());
	EXPECT_GT(candidates.expectedBerserkActionValue(), 0.0f);

	const auto * sourceUnit = sourceForecast->battleGetUnitByID(berserker->unitId());
	const auto completedBonuses = newHorizonsBerserk::completedForcedActivationBonuses(*sourceForecast, sourceUnit);
	ASSERT_EQ(completedBonuses.size(), 1u);
	EXPECT_TRUE(sourceUnit->hasBonus(liveBerserkSource));

	// Mirror the AI's queued-activation replay: remove the exact shared-helper
	// result in a child branch after scoring, without changing the source branch
	// or live authoritative stack.
	auto replay = std::make_shared<HypotheticBattle>(environment.get(), sourceForecast);
	replay->removeUnitBonus(berserker->unitId(), completedBonuses);
	const auto * replayUnit = replay->battleGetUnitByID(berserker->unitId());
	ASSERT_NE(replayUnit, nullptr);
	EXPECT_FALSE(replayUnit->hasBonus(liveBerserkSource));
	EXPECT_TRUE(replayUnit->hasBonus(intrinsicSource));
	EXPECT_TRUE(replayUnit->hasBonus(otherSpellForcedSource));
	EXPECT_TRUE(sourceForecast->battleGetUnitByID(berserker->unitId())->hasBonus(liveBerserkSource));
	EXPECT_TRUE(berserker->hasBonus(liveBerserkSource));
}

TEST_F(NewHorizonsBerserkAITest, FullEvaluatorChoosesBerserkWhenItPreventsAShotAndKeepsScoringReadOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepareBerserkCaster());
	auto * active = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(1, 5), 1);
	auto * berserker = addStack(BattleSide::DEFENDER,
		creatureByName("core:archer"), BattleHex(8, 5), 20);
	auto * enemyAlly = addStack(BattleSide::DEFENDER,
		creatureByName("core:ogre"), BattleHex(9, 5), 30);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(berserker, nullptr);
	ASSERT_NE(enemyAlly, nullptr);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto berserkerHealthBefore = berserker->getAvailableHealth();
	const auto allyHealthBefore = enemyAlly->getAvailableHealth();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto serverRandomBefore = serializedState(*gameHandler->randomizer);
	const auto defaultRandomBefore = serializedState(CRandomGenerator::getDefault());
	const auto * berserkSpell = SpellID(SpellID::BERSERK).toSpell();
	ASSERT_TRUE(berserkSpell->canBeCast(
		battle(), spells::Mode::HERO, attackerSideHero, false));

	auto callback = std::make_shared<BerserkCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0),
		BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	const bool choseAction = evaluator.attemptCastingSpell(active);
	ASSERT_TRUE(choseAction);
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, SpellID::BERSERK);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().unitValue, berserker->unitId());

	// Candidate scoring reads only detached battle state and deterministic
	// expected tied outcomes. It does not commit the spell or consume either RNG.
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(berserker->getAvailableHealth(), berserkerHealthBefore);
	EXPECT_EQ(enemyAlly->getAvailableHealth(), allyHealthBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(serializedState(*gameHandler->randomizer), serverRandomBefore);
	EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), defaultRandomBefore);
	EXPECT_FALSE(berserker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));

	const auto spellCost = battle()->battleGetSpellCost(berserkSpell, attackerSideHero);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - spellCost);
	EXPECT_TRUE(berserker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	EXPECT_EQ(enemyAlly->getAvailableHealth(), allyHealthBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
}

TEST_F(NewHorizonsBerserkAITest, ExhaustedOptionalSearchRetainsAnAuthoritativelyLegalForcedAttack)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 5);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(9, 5), 5);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(enemy, nullptr);
	addBerserkBonus(actor);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = actor->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	const auto forced = battle()->getBerserkForcedActions(actor);
	ASSERT_FALSE(forced.empty());
	ASSERT_EQ(forced.front().type, EActionType::WALK_AND_ATTACK);
	auto callback = std::make_shared<BerserkCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	branch->setSearchBudget(std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{0, 0, 0, 1000, 0}));
	DamageCache damage;
	const auto serverRandom = serializedState(*gameHandler->randomizer);
	const auto defaultRandom = serializedState(CRandomGenerator::getDefault());
	const auto actorHealth = actor->getAvailableHealth(), enemyHealth = enemy->getAvailableHealth();
	BattleEvaluator evaluator(environment, callback, branch, damage, actor, PlayerColor(0),
		BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	const auto chosen = evaluator.selectStackAction(actor);
	EXPECT_EQ(chosen.actionType, EActionType::WALK_AND_ATTACK);
	EXPECT_EQ(chosen.getTarget(battle()).front().hexValue, forced.front().target->getPosition());
	EXPECT_EQ(actor->getAvailableHealth(), actorHealth);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
	EXPECT_EQ(serializedState(*gameHandler->randomizer), serverRandom);
	EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), defaultRandom);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), chosen));
}

TEST_F(NewHorizonsBerserkAITest, ExhaustionAfterACompletedForcedAttackAlwaysRemovesScopedSpeed)
{
	CStack * actor = nullptr;
	CStack * originalTarget = nullptr;
	ASSERT_NO_FATAL_FAILURE(prepareActualBerserk(true, actor, originalTarget));
	const auto radius = BattleHex::getDistance(actor->getPosition(), originalTarget->getPosition());
	CStack * tiedTarget = nullptr;
	for(int index = 0; index != GameConstants::BFIELD_SIZE && !tiedTarget; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isAvailable() || battle()->battleGetUnitByPos(hex, true)
			|| BattleHex::getDistance(actor->getPosition(), hex) != radius) continue;
		auto * probe = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 20);
		auto trial = makeForecast();
		const auto bonus = newHorizonsBerserk::forcedActivationSpeedBonus(*trial, actor);
		ASSERT_TRUE(bonus);
		trial->addUnitBonus(actor->unitId(), {*bonus});
		const auto forced = trial->getBerserkForcedActions(trial->battleGetUnitByID(actor->unitId()));
		if(forced.size() > 1) tiedTarget = probe;
		else
		{
			BattleUnitsChanged remove;
			remove.battleID = BattleID(0);
			remove.changedStacks.emplace_back(probe->unitId(), UnitChanges::EOperation::REMOVE);
			gameHandler->sendAndApply(remove);
		}
	}
	ASSERT_NE(tiedTarget, nullptr);
	auto branch = makeForecast();
	const auto speed = branch->battleGetUnitByID(actor->unitId())->getMovementRange();
	auto budget = std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{1, 0, 0, 100000, 10000});
	branch->setSearchBudget(budget);
	DamageCache damage;
	PotentialTargets targets(branch->battleGetUnitByID(actor->unitId()), damage, branch);
	ASSERT_EQ(targets.possibleAttacks.size(), 1u);
	EXPECT_GT(budget->refusals()[0], 0u);
	for(const auto & attack : targets.possibleAttacks)
	{
		ASSERT_NE(attack.effectPreview, nullptr);
		EXPECT_EQ(attack.effectPreview->battleGetUnitByID(actor->unitId())->getMovementRange(), speed);
	}
	EXPECT_EQ(branch->battleGetUnitByID(actor->unitId())->getMovementRange(), speed);
	EXPECT_EQ(actor->getMovementRange(), speed);
}

TEST_F(NewHorizonsBerserkAITest, NativeSubjectReachabilityCacheMissChargesCurrentMovementOwner)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(enemy, nullptr);
	auto branch = makeForecast();
	auto work = std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{1000, 0, 0, 50000, 1000});
	branch->setSearchBudget(work);
	std::vector<battle::Units> order;
	branch->battleGetTurnOrder(order, 0, 2);
	ReachabilityMapCache cache;
	cache.update(order, branch);
	const auto before = work->spent();
	const auto live = gameState()->saveToMemory();
	auto native = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	cache.getOneTurnReachableUnits(native, forecastEnvironment, order, 0, enemy->getPosition());
	EXPECT_GT(work->spent()[4], before[4]);
	EXPECT_GT(work->spent()[3], before[3]);
	EXPECT_EQ(gameState()->saveToMemory(), live);
	ASSERT_TRUE(work->consume(battleSearch::WorkKind::Movement, work->available()[4]));
	const auto exhausted = work->spent();
	EXPECT_THROW(cache.getOneTurnReachableUnits(native, forecastEnvironment, order, 0, actor->getPosition()),
		battleSearch::BudgetExhausted);
	EXPECT_EQ(work->spent()[3], exhausted[3]) << "No branch capture before Movement admission";
	EXPECT_EQ(gameState()->saveToMemory(), live);
}

TEST_F(NewHorizonsBerserkAITest, ZeroMovementForcedContinuationUsesAuthoritativelyAcceptedPass)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(enemy, nullptr);
	addBerserkBonus(actor);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -1000, BonusSourceID()));
	ASSERT_EQ(actor->getMovementRange(), 0);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = actor->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	ASSERT_TRUE(battle()->getBerserkForcedActions(actor).empty());
	auto callback = std::make_shared<BerserkCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<BerserkEnvironment>(gameState());
	auto view = callback->getBattle(BattleID(0));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), view);
	auto work = std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{});
	branch->setSearchBudget(work);
	DamageCache damage;
	const auto live = gameState()->saveToMemory();
	BattleEvaluator evaluator(environment, callback, branch, damage, actor, PlayerColor(0),
		BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	const auto selected = evaluator.selectStackAction(actor);
	EXPECT_EQ(selected.actionType, EActionType::NO_ACTION);
	EXPECT_EQ(battleSearch::visibleIncumbent(environment.get(), view, actor).actionType, EActionType::NO_ACTION);
	EXPECT_EQ(gameState()->saveToMemory(), live);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), selected));
}

TEST_F(NewHorizonsBerserkAITest, ExplicitReservedOwnerPaysCaptureWhenParentHasNoUnreservedWork)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10), nullptr);
	auto parent = makeForecast();
	const auto captureUnits = static_cast<uint32_t>(parent->battleGetAllUnits(false).size()) + 1;
	auto root = std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{0, 0, 0, captureUnits + 4, 0});
	battleSearch::WorkAmounts quota{};
	quota[3] = captureUnits;
	auto reserved = root->reserve(quota);
	ASSERT_TRUE(reserved);
	auto child = std::make_shared<battleSearch::SearchBudget>(std::move(*reserved));
	ASSERT_TRUE(root->consume(battleSearch::WorkKind::Node, root->available()[3]));
	parent->setSearchBudget(root);
	const auto live = gameState()->saveToMemory();
	std::shared_ptr<HypotheticBattle> result;
	ASSERT_NO_THROW(result = std::make_shared<HypotheticBattle>(forecastEnvironment.get(), parent,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>(), child));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result->searchBudget(), child);
	EXPECT_EQ(child->spent()[3], captureUnits);
	EXPECT_EQ(root->available()[3], 0u);
	EXPECT_EQ(gameState()->saveToMemory(), live);
}

TEST_F(NewHorizonsBerserkAITest, RejectedMeleeGeometrySpendsWorkWithoutCompletingAnAttack)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 10);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(15, 5), 10), nullptr);
	auto branch = makeForecast();
	auto work = std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{100, 0, 0, 100, 1});
	branch->setSearchBudget(work);
	const auto live = gameState()->saveToMemory();
	DamageCache damage;
	PotentialTargets targets(branch->battleGetUnitByID(actor->unitId()), damage, branch);
	EXPECT_TRUE(targets.possibleAttacks.empty());
	EXPECT_GT(work->spent()[3], 0u) << "Rejected defender admission is charged";
	EXPECT_EQ(work->spent()[4], 1u);
	EXPECT_GT(work->refusals()[4], 0u) << "Rejected hex probes cannot scan past the cap";
	EXPECT_EQ(gameState()->saveToMemory(), live);
}

#ifdef NH_PERF_EXPERIMENTS
#include "../../AI/BattleAI/SearchWorkDiagnostics.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../lib/battle/AutocombatPreferences.h"
namespace
{
class SearchDiagnosticCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	SearchDiagnosticCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};
}
TEST_F(NewHorizonsBerserkAITest, DiagnosticRootIncludesNativeNestedWorkAndRefusalAfterChildrenSettle)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(enemy, nullptr);
	auto source = makeForecast();
	battleSearch::SearchLimits limits;
	limits.decision = {32, 8, 4, 8192, 256};
	battleSearch::RootSearchWorkCapture capture;
	auto root = capture.begin(battleSearch::Profile::Full, limits);
	const auto live = gameState()->saveToMemory();
	const auto authorityRng = serializedState(*gameHandler->randomizer);
	const auto aiRng = serializedState(CRandomGenerator::getDefault());
	{
		auto reservation = root->reserve({8, 0, 0, 4096, 128});
		ASSERT_TRUE(reservation);
		auto child = std::make_shared<battleSearch::SearchBudget>(std::move(*reservation));
		auto branch = std::make_shared<HypotheticBattle>(forecastEnvironment.get(), source,
			std::shared_ptr<const battle::OwnedBattleFrameBatch>(), child);
		auto nested = std::make_shared<HypotheticBattle>(forecastEnvironment.get(), branch);
		EXPECT_EQ(nested->searchBudget(), child);
		std::vector<battle::Units> order;
		nested->battleGetTurnOrder(order, 0, 2);
		ReachabilityMapCache cache;
		cache.update(order, nested);
		ASSERT_TRUE(child->consume(battleSearch::WorkKind::Movement, child->available()[4]));
		auto native = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		EXPECT_THROW(cache.getOneTurnReachableUnits(native, forecastEnvironment, order, 0, enemy->getPosition()),
			battleSearch::BudgetExhausted);
		// Additional root-only work proves the snapshot is not the child's copy.
		root->require(battleSearch::WorkKind::Node);
		EXPECT_GT(root->spent()[3], child->spent()[3]);
	}
	const auto spent = root->spent();
	const auto refused = root->refusals();
	root.reset(); // The receipt itself must retain the decision owner.
	capture.complete();
	capture.complete(); // Completing an already published decision is idempotent.
	CBattleAI::TimingSummary sink;
	sink.searchWork.accumulate(capture.summary());
	EXPECT_EQ(sink.searchWork.startedDecisions, 1u);
	EXPECT_EQ(sink.searchWork.completedFiniteDecisions, 1u);
	EXPECT_EQ(sink.searchWork.profileMask, 1u);
	for(size_t i = 0; i < battleSearch::workKindCount; ++i)
	{
		EXPECT_EQ(sink.searchWork.spent[i], spent[i]);
		EXPECT_EQ(sink.searchWork.refusals[i], refused[i]);
	}
	EXPECT_GT(sink.searchWork.spent[3], 0u);
	EXPECT_GT(sink.searchWork.spent[4], 0u);
	EXPECT_GT(sink.searchWork.refusals[4], 0u);
	EXPECT_EQ(gameState()->saveToMemory(), live);
	EXPECT_EQ(serializedState(*gameHandler->randomizer), authorityRng);
	EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), aiRng);
}
TEST_F(NewHorizonsBerserkAITest, DiagnosticRootSurvivesConstructorExhaustionAndAuthoritativelyLegalFallback)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10), nullptr);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = actor->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	auto source = makeForecast();
	battleSearch::SearchLimits limits;
	limits.decision = {};
	battleSearch::RootSearchWorkCapture capture;
	auto root = capture.begin(battleSearch::Profile::Full, limits);
	const auto live = gameState()->saveToMemory();
	EXPECT_THROW(std::make_shared<HypotheticBattle>(forecastEnvironment.get(), source,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>(), root), battleSearch::BudgetExhausted);
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	const auto fallback = battleSearch::visibleIncumbent(forecastEnvironment.get(), callback, actor);
	capture.complete();
	CBattleAI::TimingSummary sink;
	sink.searchWork.accumulate(capture.summary());
	EXPECT_EQ(sink.searchWork.completedFiniteDecisions, 1u);
	EXPECT_EQ(sink.searchWork.missingRootDecisions, 0u);
	EXPECT_GT(sink.searchWork.refusals[3], 0u);
	EXPECT_EQ(gameState()->saveToMemory(), live);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), fallback));
}
TEST_F(NewHorizonsBerserkAITest, ActualAICallbackPublishesRootWorkWithFullAndFastIdentity)
{
	ASSERT_NO_FATAL_FAILURE(startEmptyBattle());
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(6, 5), 10), nullptr);
	auto source = makeForecast();
	const auto live = gameState()->saveToMemory();
	const auto authorityRng = serializedState(*gameHandler->randomizer);
	const auto aiRng = serializedState(CRandomGenerator::getDefault());
	for(auto profile : {battleSearch::Profile::Full, battleSearch::Profile::BeamOneTwoActors})
	{
		auto callback = std::make_shared<SearchDiagnosticCallback>();
		callback->onBattleStarted(battle());
		CBattleAI ai(nullptr, nullptr, profile);
		AutocombatPreferences preferences;
		preferences.enableSpellsUsage = false;
		preferences.enableTacticsUsage = false;
		ai.initBattleInterface(forecastEnvironment, callback, preferences);
		ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		ai.activeStack(BattleID(0), actor);
		ASSERT_EQ(callback->submitted.size(), 1u);
		const auto timing = ai.getTimingSummary();
		ASSERT_TRUE(timing);
		EXPECT_GT(timing->searchWork.startedDecisions, 0u);
		EXPECT_EQ(timing->searchWork.startedDecisions, timing->searchWork.completedFiniteDecisions);
		EXPECT_EQ(timing->searchWork.profileMask, profile == battleSearch::Profile::Full ? 1u : 4u);
		EXPECT_EQ(timing->searchWork.missingRootDecisions, 0u);
		EXPECT_EQ(timing->searchWork.unsupportedDecisions, 0u);
		EXPECT_GT(timing->searchWork.spent[3], 0u);
		EXPECT_EQ(gameState()->saveToMemory(), live);
		EXPECT_EQ(serializedState(*gameHandler->randomizer), authorityRng);
		EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), aiRng);
	}
}
TEST(SearchWorkDiagnostics, MissingAndUnsupportedOwnersAreDistinctFromMeasuredZero)
{
	battleSearch::RootSearchWorkCapture unsupported;
	EXPECT_FALSE(unsupported.begin(battleSearch::Profile::LegacyUnlimited, std::nullopt));
	unsupported.complete();
	EXPECT_EQ(unsupported.summary().unsupportedDecisions, 1u);
	EXPECT_EQ(unsupported.summary().completedFiniteDecisions, 0u);
	EXPECT_EQ(unsupported.summary().profileMask, 8u);
	battleSearch::RootSearchWorkCapture missing;
	EXPECT_FALSE(missing.begin(battleSearch::Profile::Full, std::nullopt));
	missing.complete();
	EXPECT_EQ(missing.summary().missingRootDecisions, 1u);
	EXPECT_EQ(missing.summary().unsupportedDecisions, 0u);
	EXPECT_EQ(missing.summary().completedFiniteDecisions, 0u);
	battleSearch::SearchWorkSummary saturated, increment;
	saturated.spent[3] = std::numeric_limits<uint64_t>::max();
	increment.spent[3] = 1;
	saturated.accumulate(increment);
	EXPECT_TRUE(saturated.saturated);
	EXPECT_EQ(saturated.spent[3], std::numeric_limits<uint64_t>::max());
}
#endif
