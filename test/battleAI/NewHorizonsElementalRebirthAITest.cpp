/*
 * NewHorizonsElementalRebirthAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/logging/CLogger.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/battle/VisibleHeroPolicy.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/battle/NewHorizonsSerpentsEmbrace.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <tuple>


namespace
{
class ElementalRebirthEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ElementalRebirthEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ElementalRebirthCallback final : public CBattleCallback
{
public:
	explicit ElementalRebirthCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
};

HeroTypeID heroType(const char * identifier)
{
	const int decoded = HeroTypeID::decode(identifier);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing hero in Elemental Rebirth fixture: ") + identifier);
	return HeroTypeID(decoded);
}

CreatureID creature(const char * identifier)
{
	const int decoded = CreatureID::decode(identifier);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing creature in Elemental Rebirth fixture: ") + identifier);
	return CreatureID(decoded);
}

const battle::Unit * unitAt(const CBattleInfoCallback & battle, BattleSide side, SlotID slot)
{
	for(const auto * unit : battle.battleGetAllUnits(false))
		if(unit && unit->unitSide() == side && unit->unitSlot() == slot)
			return unit;
	return nullptr;
}

std::optional<newHorizonsElementalRebirth::DeathSnapshot> captureAndKillSource(
	HypotheticBattle & projected, uint32_t sourceId)
{
	const auto * source = projected.battleGetUnitByID(sourceId);
	if(!source)
		return {};
	auto snapshot = projected.captureElementalRebirthSource(*source);
	if(!snapshot)
		return {};
	auto projectedSource = projected.getForUpdate(sourceId);
	int64_t lethalDamage = projectedSource->getAvailableHealth();
	projectedSource->damage(lethalDamage);
	if(projected.battleGetUnitByID(sourceId)->alive())
		return {};
	return snapshot;
}

std::vector<const battle::Unit *> rebirthSpawns(const HypotheticBattle & battle)
{
	std::vector<const battle::Unit *> result;
	for(const auto unitId : battle.getElementalRebirthSpawnUnitIds())
		if(const auto * unit = battle.battleGetUnitByID(unitId))
			result.push_back(unit);
	return result;
}

bool moveUnitAdjacent(HypotheticBattle & battle, uint32_t movingUnitId, uint32_t centerUnitId)
{
	const auto * center = battle.battleGetUnitByID(centerUnitId);
	if(!center)
		return false;
	for(const auto & hex : center->getSurroundingHexes())
		if(hex.isAvailable() && !battle.battleGetUnitByPos(hex, true))
		{
			battle.moveUnit(movingUnitId, hex);
			return true;
		}
	return false;
}
}

class NewHorizonsElementalRebirthAITest : public HeroCommandFixture
{
protected:
	static constexpr auto REBIRTH_SKILL = "new-horizons:elementalRebirth";
	static constexpr auto PRIMAL_BURST = "new-horizons:elementalRebirth.primalBurst";
	static constexpr auto GREATER_ESSENCE = "new-horizons:elementalRebirth.greaterEssence";
	static constexpr auto ELEMENTAL_WARD = "new-horizons:elementalRebirth.elementalWard";
	bool neutralObserver = false;
	int armyHealthBonus = 0;
	CGHeroInstance * confluxHero = nullptr;
	const battle::Unit * source = nullptr;
	const battle::Unit * survivingAlly = nullptr;
	const battle::Unit * opposingPikemen = nullptr;
	std::shared_ptr<ElementalRebirthEnvironment> environment;
	std::shared_ptr<ElementalRebirthCallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons native profile";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(MasteryLevel::Type rebirthRank = MasteryLevel::BASIC, int sourceCount = 1,
		const char * advancedPerk = nullptr, bool selectPrimalBurst = false, int attackerCount = 100)
	{
		startGame();
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		defenderSideHero->setHeroType(heroType("core:brissa"));
		confluxHero = defenderSideHero;
		ASSERT_EQ(confluxHero->getFactionID(), FactionID::CONFLUX);

		const int rebirthSkillId = SecondarySkill::decode(REBIRTH_SKILL);
		ASSERT_GE(rebirthSkillId, 0);
		confluxHero->setSecSkillLevel(SecondarySkill(rebirthSkillId), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		if(selectPrimalBurst || advancedPerk)
		{
			confluxHero->applyPerkSelection({REBIRTH_SKILL, PRIMAL_BURST});
			ASSERT_TRUE(confluxHero->hasActivePerk(REBIRTH_SKILL, PRIMAL_BURST));
		}
		if(advancedPerk)
		{
			confluxHero->setSecSkillLevel(SecondarySkill(rebirthSkillId), MasteryLevel::ADVANCED,
				ChangeValueMode::ABSOLUTE);
			confluxHero->applyPerkSelection({REBIRTH_SKILL, advancedPerk});
			ASSERT_TRUE(confluxHero->hasActivePerk(REBIRTH_SKILL, advancedPerk));
		}
		if(rebirthRank != MasteryLevel::BASIC
			&& !(advancedPerk && rebirthRank == MasteryLevel::ADVANCED))
			confluxHero->setSecSkillLevel(SecondarySkill(rebirthSkillId), rebirthRank,
				ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(confluxHero->usesNewHorizonsMovement());
		const auto sourceProfile = newHorizonsElementalRebirth::activeProfile(confluxHero);
		ASSERT_TRUE(sourceProfile.has_value());
		ASSERT_EQ(sourceProfile->rank, static_cast<int>(rebirthRank));
		ASSERT_EQ(sourceProfile->primalBurst, selectPrimalBurst || advancedPerk);
		ASSERT_EQ(sourceProfile->greaterEssence,
			advancedPerk && std::string_view(advancedPerk) == GREATER_ESSENCE);
		ASSERT_EQ(sourceProfile->elementalWard,
			advancedPerk && std::string_view(advancedPerk) == ELEMENTAL_WARD);

		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), attackerCount));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:peasant"), sourceCount));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creature("core:peasant"), 1000));
		giveArtifact(confluxHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		confluxHero->removeAllSpells();
		confluxHero->addSpellToSpellbook(SpellID(SpellID::FIREBALL));
		confluxHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
		confluxHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(confluxHero, 100);

		if(neutralObserver) attackerSideHero->setOwner(PlayerColor::NEUTRAL);
		if(armyHealthBonus)
			confluxHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::STACK_HEALTH, BonusSource::OTHER, armyHealthBonus, BonusSourceID()));
		startBattle();
		source = unitAt(*battle(), BattleSide::DEFENDER, SlotID(0));
		survivingAlly = unitAt(*battle(), BattleSide::DEFENDER, SlotID(1));
		opposingPikemen = unitAt(*battle(), BattleSide::ATTACKER, SlotID(0));
		ASSERT_NE(source, nullptr);
		ASSERT_NE(survivingAlly, nullptr);
		ASSERT_NE(opposingPikemen, nullptr);
		ASSERT_GT(source->getBattleStartMaximumAggregateHP(), 0);
		if(neutralObserver)
		{
			// The shared helper submits as red; this real neutral attacker must
			// end deployment through the same validated action as its actual owner.
			battle()->tacticDistance = 1;
			battle()->tacticsSide = BattleSide::ATTACKER;
			const auto action = BattleAction::makeEndOFTacticPhase(BattleSide::ATTACKER);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->getSidePlayer(BattleSide::ATTACKER), action));
			ASSERT_EQ(battle()->tacticDistance, 0);
		}
		else
			beginCombat();

		callback = std::make_shared<ElementalRebirthCallback>(battle()->getSidePlayer(BattleSide::DEFENDER));
		callback->onBattleStarted(battle());
		ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetFightingHero(BattleSide::DEFENDER), confluxHero);
		environment = std::make_shared<ElementalRebirthEnvironment>(gameState());
	}
};


TEST_F(NewHorizonsElementalRebirthAITest, EmptyHexReplayPreservesEligibleBrissaSourceAndInvalidatesLifeDrain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto actor = child->getForUpdate(source->unitId());
	const auto * target = child->battleGetUnitByID(opposingPikemen->unitId());
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(child->captureElementalRebirthSource(*actor).has_value());
	const auto triggers = actor->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	ASSERT_TRUE(triggers->empty());
	const auto bonuses = [](const battle::Unit & unit)
	{
		std::vector<JsonNode> result;
		const auto owner = unit.getUnstackedBonuses(Selector::all);
		for(const auto & bonus : *owner)
			result.push_back(bonus->toJsonNode());
		return result;
	};
	const auto savedBranch = [&](const HypotheticBattle & model)
	{
		const auto * a = model.battleGetUnitByID(source->unitId());
		const auto * b = model.battleGetUnitByID(opposingPikemen->unitId());
		return std::tuple{a->acquireState()->save(), b->acquireState()->save(),
			a->getAvailableHealth(), b->getAvailableHealth(), bonuses(*a), bonuses(*b),
			model.getRebirthChainUsed(BattleSide::ATTACKER), model.getRebirthChainUsed(BattleSide::DEFENDER),
			model.getPhoenixSparkUsed(BattleSide::ATTACKER), model.getPhoenixSparkUsed(BattleSide::DEFENDER),
			model.getElementalRebirthSpawnUnitIds()};
	};
	const auto liveState = std::tuple{source->acquireState()->save(), opposingPikemen->acquireState()->save(),
		bonuses(*source), bonuses(*opposingPikemen)};
	const auto parentBefore = savedBranch(*parent);
	const auto siblingBefore = savedBranch(*sibling);
	const auto childBefore = savedBranch(*child);
	const auto rngBytes = [&]()
	{
		CMemorySerializer server, fixture, defaults;
		server.oser & *gameHandler->randomizer;
		fixture.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		defaults.oser & CRandomGenerator::getDefault();
		return std::tuple{server.extractBuffer(), fixture.extractBuffer(), defaults.extractBuffer()};
	};
	const auto rngBefore = rngBytes();
	BattleAttackInfo attack(actor.get(), target, 0, false);
	ASSERT_TRUE(child->beginLifeDrainFriendlyObservation(actor->unitId()));
	EXPECT_EQ(child->projectHexOfPainStrike(attack, {{target->unitId(), 1}}, 0), 0);
	EXPECT_FALSE(child->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(savedBranch(*child), childBefore);
	EXPECT_EQ(savedBranch(*parent), parentBefore);
	EXPECT_EQ(savedBranch(*sibling), siblingBefore);
	EXPECT_EQ((std::tuple{source->acquireState()->save(), opposingPikemen->acquireState()->save(),
		bonuses(*source), bonuses(*opposingPikemen)}), liveState);
	EXPECT_EQ(rngBytes(), rngBefore);
	EXPECT_TRUE(child->captureElementalRebirthSource(*actor).has_value());
}

TEST_F(NewHorizonsElementalRebirthAITest, ProjectsSpellKilledSourceAsTemporaryStackAtExactBattleStartFraction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto originalCount = battle()->battleGetAllUnits(false).size();
	const auto originalHealth = source->getAvailableHealth();
	const auto sourceId = source->unitId();

	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto * projectedSource = projected->battleGetUnitByID(sourceId);
	ASSERT_NE(projectedSource, nullptr);
	ASSERT_TRUE(newHorizonsElementalRebirth::isEligibleSource(*projectedSource));
	ASSERT_EQ(projectedSource->getBattleStartMaximumAggregateHP(), source->getBattleStartMaximumAggregateHP());
	ASSERT_GT(projectedSource->getBattleStartMaximumAggregateHP(), 0);
	const auto * visibleSourceHero = projected->battleGetFightingHero(projectedSource->unitSide());
	ASSERT_EQ(visibleSourceHero, confluxHero);
	const auto projectedProfile = newHorizonsElementalRebirth::activeProfile(visibleSourceHero);
	ASSERT_TRUE(projectedProfile.has_value());
	ASSERT_EQ(projectedProfile->rank, 1);
	const auto snapshot = projected->captureElementalRebirthSource(*projectedSource);
	ASSERT_TRUE(snapshot.has_value());
	ASSERT_EQ(snapshot->profile.rank, 1);
	ASSERT_EQ(snapshot->battleStartMaximumAggregateHP, source->getBattleStartMaximumAggregateHP());
	const auto expectedHP = newHorizonsElementalRebirth::targetHP(*snapshot);
	const auto legalPool = newHorizonsElementalRebirth::legalCandidatePool(
		projected->getCreatureCategoryRules(), projected->getAccessibility(projectedSource),
		snapshot->corpsePosition, snapshot->side);
	ASSERT_FALSE(legalPool.empty());

	const auto fireball = SpellID(SpellID::FIREBALL);
	const auto * spell = fireball.toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(projected.get(), confluxHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	const spells::Target target{spells::Destination(snapshot->corpsePosition)};
	spells::detail::ProblemImpl castProblem;
	const bool canCast = mechanics->canBeCastAt(target, castProblem);
	std::vector<std::string> castProblems;
	castProblem.getAll(castProblems);
	ASSERT_TRUE(canCast) << ::testing::PrintToString(castProblems);
	mechanics->castEval(projected->getServerCallback(), target);
	EXPECT_FALSE(projected->battleGetUnitByID(sourceId)->alive());
	EXPECT_TRUE(projected->battleGetUnitByID(survivingAlly->unitId())->alive())
		<< "The adjacent collateral stack has enough units to survive this low-power Fireball";

	const auto spawns = rebirthSpawns(*projected);
	ASSERT_EQ(spawns.size(), 1u);
	const auto * reborn = spawns.front();
	EXPECT_TRUE(reborn->isSummoned());
	EXPECT_FALSE(reborn->isGhost());
	EXPECT_EQ(reborn->unitSide(), BattleSide::DEFENDER);
	EXPECT_EQ(reborn->getPosition(), snapshot->corpsePosition);
	EXPECT_EQ(reborn->getAvailableHealth(), expectedHP)
		<< "The private cast projection must preserve the exact floored battle-start HP pool";
	EXPECT_TRUE(vstd::contains(legalPool, reborn->creatureId()));
	EXPECT_FALSE(projected->captureElementalRebirthSource(*reborn).has_value())
		<< "A temporary Rebirth stack must not recursively become a source";
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), originalCount)
		<< "Detached spell projection must not add a live battle stack";
	EXPECT_EQ(source->getAvailableHealth(), originalHealth)
		<< "Detached spell projection must not injure the live source";
}

TEST_F(NewHorizonsElementalRebirthAITest, PreservesExactOriginalHPAcrossProjectedSpawnDamageAndBranches)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 317));
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto snapshot = captureAndKillSource(*projected, source->unitId());
	ASSERT_TRUE(snapshot.has_value());
	const auto expectedOriginalHP = newHorizonsElementalRebirth::targetHP(*snapshot);
	ASSERT_GT(expectedOriginalHP, 0);
	ASSERT_TRUE(projected->projectElementalRebirth(
		projected->battleGetUnitByID(source->unitId()), *snapshot, true, false, false));

	const auto spawns = rebirthSpawns(*projected);
	ASSERT_EQ(spawns.size(), 1u);
	const auto * reborn = spawns.front();
	const auto spawnId = reborn->unitId();
	EXPECT_EQ(reborn->getAvailableHealth(), expectedOriginalHP);
	EXPECT_EQ(reborn->getRebirthOriginalAggregateHP(), expectedOriginalHP)
		<< "The actual projected spawn must retain the exact immutable target from UnitInfo";
	ASSERT_GT(reborn->getMaxHealth(), 0);
	ASSERT_NE(expectedOriginalHP % reborn->getMaxHealth(), 0)
		<< "Use a target pool that does not divide evenly into the selected elemental stack";

	const auto * rebornState = dynamic_cast<const battle::CUnitState *>(reborn);
	ASSERT_NE(rebornState, nullptr);
	StackWithBonuses copiedFromState(projected.get(), rebornState);
	EXPECT_EQ(copiedFromState.getRebirthOriginalAggregateHP(), expectedOriginalHP);
	StackWithBonuses assignedFromState(projected.get(), source);
	assignedFromState = *rebornState;
	EXPECT_EQ(assignedFromState.getRebirthOriginalAggregateHP(), expectedOriginalHP);

	auto childProjection = std::make_shared<HypotheticBattle>(environment.get(), projected);
	const auto * childReborn = childProjection->battleGetUnitByID(spawnId);
	ASSERT_NE(childReborn, nullptr);
	EXPECT_EQ(childReborn->getRebirthOriginalAggregateHP(), expectedOriginalHP);
	auto changedChild = childProjection->getForUpdate(spawnId);
	EXPECT_EQ(changedChild->getRebirthOriginalAggregateHP(), expectedOriginalHP);
	int64_t laterDamage = 1;
	changedChild->damage(laterDamage);
	EXPECT_EQ(changedChild->getAvailableHealth(), expectedOriginalHP - 1);
	EXPECT_EQ(changedChild->getRebirthOriginalAggregateHP(), expectedOriginalHP)
		<< "Later damage changes current health, not the first-generation HP identity";
	EXPECT_EQ(reborn->getAvailableHealth(), expectedOriginalHP)
		<< "A child projection's damage must leave its parent branch untouched";
}

TEST_F(NewHorizonsElementalRebirthAITest, SelectedGreaterEssenceAddsFifteenPointsAtAdvancedAndExpert)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED, 100, GREATER_ESSENCE, true));
	const auto basis = source->getBattleStartMaximumAggregateHP();
	ASSERT_GT(basis, 0);

	auto advancedProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto advancedSnapshot = captureAndKillSource(*advancedProjection, source->unitId());
	ASSERT_TRUE(advancedSnapshot.has_value());
	EXPECT_TRUE(advancedSnapshot->profile.primalBurst);
	EXPECT_TRUE(advancedSnapshot->profile.greaterEssence);
	EXPECT_FALSE(advancedSnapshot->profile.elementalWard);
	ASSERT_TRUE(advancedProjection->projectElementalRebirth(
		advancedProjection->battleGetUnitByID(source->unitId()), *advancedSnapshot, true, false, false));
	auto advancedSpawns = rebirthSpawns(*advancedProjection);
	ASSERT_EQ(advancedSpawns.size(), 1u);
	EXPECT_EQ(advancedSpawns.front()->getAvailableHealth(), basis * 55 / 100);

	const int rebirthSkillId = SecondarySkill::decode(REBIRTH_SKILL);
	confluxHero->setSecSkillLevel(SecondarySkill(rebirthSkillId), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(confluxHero->hasActivePerk(REBIRTH_SKILL, GREATER_ESSENCE));
	auto expertProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto expertSnapshot = captureAndKillSource(*expertProjection, source->unitId());
	ASSERT_TRUE(expertSnapshot.has_value());
	EXPECT_EQ(expertSnapshot->profile.rank, static_cast<int>(MasteryLevel::EXPERT));
	EXPECT_TRUE(expertSnapshot->profile.greaterEssence);
	ASSERT_TRUE(expertProjection->projectElementalRebirth(
		expertProjection->battleGetUnitByID(source->unitId()), *expertSnapshot, true, false, false));
	const auto expertSpawns = rebirthSpawns(*expertProjection);
	ASSERT_EQ(expertSpawns.size(), 1u);
	EXPECT_EQ(expertSpawns.front()->getAvailableHealth(), basis * 65 / 100);

	EXPECT_EQ(source->getAvailableHealth(), basis)
		<< "Greater Essence projections must leave the authoritative source untouched";
}

TEST_F(NewHorizonsElementalRebirthAITest, WardAndPrimalBurstUseTheSharedProjectedMagicMitigation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::ADVANCED, 100, ELEMENTAL_WARD, true));
	const auto sourceId = source->unitId();
	const auto targetId = opposingPikemen->unitId();
	const auto sourceHealth = source->getAvailableHealth();
	const auto targetHealth = opposingPikemen->getAvailableHealth();
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	ASSERT_TRUE(moveUnitAdjacent(*projected, targetId, sourceId));

	const auto hostileIds = newHorizonsElementalRebirth::adjacentHostileUnitIds(
		*projected, *projected->battleGetUnitByID(sourceId));
	ASSERT_EQ(hostileIds, std::vector<uint32_t>{targetId});
	const auto snapshot = captureAndKillSource(*projected, sourceId);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_TRUE(snapshot->profile.primalBurst);
	EXPECT_FALSE(snapshot->profile.greaterEssence);
	EXPECT_TRUE(snapshot->profile.elementalWard);
	ASSERT_TRUE(projected->projectElementalRebirth(
		projected->battleGetUnitByID(sourceId), *snapshot, true, false, false));

	const auto spawns = rebirthSpawns(*projected);
	ASSERT_EQ(spawns.size(), 1u);
	const auto * reborn = spawns.front();
	const auto expectedRebornHP = source->getBattleStartMaximumAggregateHP() * 40 / 100;
	EXPECT_EQ(reborn->getAvailableHealth(), expectedRebornHP);
	EXPECT_EQ(reborn->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS), 2000);
	ASSERT_EQ(projected->getProjectedPrimalBurstHits().size(), 1u);
	const auto & burstHit = projected->getProjectedPrimalBurstHits().front();
	EXPECT_EQ(burstHit.preHitTarget->unitId(), targetId);
	EXPECT_EQ(burstHit.targetController, battle()->battleGetOwner(opposingPikemen));
	EXPECT_EQ(burstHit.actualDamage, newHorizonsElementalRebirth::primalBurstDamageBudget(expectedRebornHP));
	EXPECT_EQ(projected->battleGetUnitByID(targetId)->getAvailableHealth(), targetHealth - burstHit.actualDamage);

	// Magic Resistance is not a substitute for the separate generic magical-damage
	// reduction supplied by Ward; the shared forecast applies exactly 20% here.
	projected->getForUpdate(reborn->unitId())->addUnitBonus({Bonus(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID())});
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*projected, *reborn, 100), 80);
	EXPECT_EQ(source->getAvailableHealth(), sourceHealth);
	EXPECT_EQ(opposingPikemen->getAvailableHealth(), targetHealth);
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), 3u)
		<< "Ward and Primal Burst must remain detached AI projection effects";
}

TEST_F(NewHorizonsElementalRebirthAITest, ReplacementChangesPhysicalExchangeScoreInsteadOfBeingDroppedAsMagicalSummon)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto sourceId = source->unitId();
	const auto attackerId = opposingPikemen->unitId();
	const auto liveSourceHealth = source->getAvailableHealth();

	auto rebirthProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache rebirthCache;
	BattleExchangeVariant rebirthExchange;
	rebirthExchange.trackAttack(rebirthProjection->getForUpdate(attackerId),
		rebirthProjection->getForUpdate(sourceId), false, false, rebirthCache, rebirthProjection,
		false, false);
	ASSERT_FALSE(rebirthProjection->battleGetUnitByID(sourceId)->alive())
		<< "The physical projection must cross the destruction transition for its source";
	const auto rebirthScore = rebirthExchange.getScore().enemyDamageReduce
		- rebirthExchange.getScore().ourDamageReduce;
	const auto spawns = rebirthSpawns(*rebirthProjection);
	ASSERT_EQ(spawns.size(), 1u);
	ASSERT_FALSE(rebirthProjection->battleGetUnitByID(sourceId)->alive());
	EXPECT_EQ(source->getAvailableHealth(), liveSourceHealth)
		<< "The selected physical branch must not mutate the authoritative battle";

	const auto * reborn = spawns.front();
	const auto enemies = rebirthProjection->battleGetUnitsIf([&](const battle::Unit * unit)
	{
		return unit && unit->alive() && !rebirthProjection->battleMatchOwner(reborn, unit);
	});
	ASSERT_FALSE(enemies.empty());
	const auto fullSpawnHP = static_cast<int64_t>(reborn->getCount()) * reborn->getMaxHealth();
	ASSERT_GT(fullSpawnHP, 0);
	const float expectedReplacementValue = static_cast<float>(rebirthCache.getOriginalDamage(
		reborn, enemies.front(), rebirthProjection)) * static_cast<float>(reborn->getAvailableHealth())
		/ static_cast<float>(fullSpawnHP);

	// The same legal enemy attack without the Conflux rank is the control. Its direct
	// damage score remains, while the own-side replacement improves our branch score.
	confluxHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(REBIRTH_SKILL)),
		MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	auto baselineProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache baselineCache;
	BattleExchangeVariant baselineExchange;
	baselineExchange.trackAttack(baselineProjection->getForUpdate(attackerId),
		baselineProjection->getForUpdate(sourceId), false, false, baselineCache, baselineProjection,
		false, false);
	const auto baselineScore = baselineExchange.getScore().enemyDamageReduce
		- baselineExchange.getScore().ourDamageReduce;
	EXPECT_TRUE(baselineProjection->getElementalRebirthSpawnUnitIds().empty());
	EXPECT_GT(expectedReplacementValue, 0.0f);
	EXPECT_GT(rebirthScore, baselineScore)
		<< "Our replacement must improve this branch's score, even for a tiny HP pool";
	EXPECT_NEAR(rebirthScore - baselineScore, expectedReplacementValue,
		std::max(1.0f, expectedReplacementValue * 0.02f))
		<< "The projected replacement must contribute exactly once in the physical DPS score scale";
}

TEST_F(NewHorizonsElementalRebirthAITest, MemoryProbePreservesScoreAndBranchesWhileAppliedDeathRetainsMemory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	constexpr auto memoryPerk = "new-horizons:elementalRebirth.elementalMemory";
	confluxHero->applyPerkSelection({REBIRTH_SKILL, memoryPerk});
	ASSERT_TRUE(confluxHero->hasActivePerk(REBIRTH_SKILL, memoryPerk));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto sourceId = source->unitId();
	const auto attackerId = opposingPikemen->unitId();
	parent->addUnitBonus(sourceId, {
		Bonus(BonusDuration::PERMANENT, BonusType::MORALE, BonusSource::OTHER, 1000, BonusSourceID()),
		Bonus(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::OTHER, 1000, BonusSourceID())});
	auto parentSource = parent->getForUpdate(sourceId);
	auto parentAttacker = parent->getForUpdate(attackerId);
	const auto snapshot = parent->captureElementalRebirthSource(*parentSource);
	ASSERT_TRUE(snapshot.has_value());
	ASSERT_TRUE(snapshot->profile.elementalMemory);
	ASSERT_GT(snapshot->positiveMoraleModifier, 0);
	ASSERT_GT(snapshot->positiveLuckModifier, 0);
	const auto parentSourceState = parentSource->save();
	const auto parentAttackerState = parentAttacker->save();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto siblingSource = sibling->getForUpdate(sourceId);
	auto siblingAttacker = sibling->getForUpdate(attackerId);
	ASSERT_EQ(siblingSource->save(), parentSourceState);
	ASSERT_EQ(siblingAttacker->save(), parentAttackerState);
	auto attacker = child->getForUpdate(attackerId);
	auto victim = child->getForUpdate(sourceId);
	ASSERT_NE(victim.get(), parentSource.get());
	ASSERT_NE(victim.get(), siblingSource.get());
	DamageCache cache;
	const auto damage = cache.getDamage(attacker.get(), victim.get(), child);
	ASSERT_GE(damage, victim->getAvailableHealth());
	const auto projectedDamage = battleAIProjectDamage(victim.get(), damage,
		battleAIDamageProvenance(attacker.get(), true));
	const auto expectedScore = AttackPossibility::calculateDamageReduce(attacker.get(), victim.get(),
		projectedDamage.healthLoss, cache, child);
	BattleExchangeVariant exchange;
	const auto probeScore = exchange.trackAttack(attacker, victim, false, false, cache, child, true, false);
	EXPECT_FLOAT_EQ(probeScore, expectedScore);
	EXPECT_TRUE(child->getElementalRebirthSpawnUnitIds().empty());
	EXPECT_EQ(victim->save(), parentSourceState);
	EXPECT_EQ(attacker->save(), parentAttackerState);

	// The same selected branch still captures before an applied lethal hit.
	exchange.trackAttack(attacker, victim, false, false, cache, child, false, false);
	ASSERT_FALSE(victim->alive());
	const auto spawns = rebirthSpawns(*child);
	ASSERT_EQ(spawns.size(), 1u);
	const auto * reborn = spawns.front();
	EXPECT_EQ(reborn->getAvailableHealth(), newHorizonsElementalRebirth::targetHP(*snapshot, reborn->creatureId(), *child));
	for(const auto type : {BonusType::MORALE, BonusType::LUCK})
	{
		const auto positive = reborn->getAllBonuses(Selector::type()(type).And(CSelector([](const Bonus * bonus)
		{
			return bonus && bonus->val > 0;
		})));
		const int captured = type == BonusType::MORALE
			? snapshot->positiveMoraleModifier : snapshot->positiveLuckModifier;
		EXPECT_EQ(positive->totalValue(), captured);
		const auto memory = reborn->getAllBonuses(Selector::type()(type).And(CSelector([](const Bonus * bonus)
		{
			return bonus && bonus->stacking.starts_with("new-horizons:elementalRebirth.elementalMemory.");
		})));
		ASSERT_EQ(memory->size(), 1u);
		EXPECT_GT(memory->front()->val, 0);
		EXPECT_EQ(memory->front()->turnsRemain, 1);
	}
	for(const auto & branch : {parent, sibling})
	{
		EXPECT_EQ(branch->getForUpdate(sourceId)->save(), parentSourceState);
		EXPECT_EQ(branch->getForUpdate(attackerId)->save(), parentAttackerState);
		EXPECT_EQ(branch->battleGetUnitByID(sourceId)->valOfBonuses(BonusType::MORALE), snapshot->positiveMoraleModifier);
		EXPECT_EQ(branch->battleGetUnitByID(sourceId)->valOfBonuses(BonusType::LUCK), snapshot->positiveLuckModifier);
		EXPECT_TRUE(branch->getElementalRebirthSpawnUnitIds().empty());
	}
	EXPECT_EQ(source->getAvailableHealth(), parentSource->getAvailableHealth());
	EXPECT_TRUE(source->alive());
}

TEST_F(NewHorizonsElementalRebirthAITest, PrimalBurstCollateralIsIncludedInPhysicalExchangeAndBranchCopies)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 400, nullptr, true, 1000));
	ASSERT_TRUE(confluxHero->hasActivePerk(REBIRTH_SKILL, PRIMAL_BURST));
	const auto sourceId = source->unitId();
	const auto attackerId = opposingPikemen->unitId();
	const auto liveSourceHealth = source->getAvailableHealth();
	const auto liveAttackerHealth = opposingPikemen->getAvailableHealth();

	auto rebirthProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	ASSERT_TRUE(moveUnitAdjacent(*rebirthProjection, attackerId, sourceId));
	const auto adjacentPosition = rebirthProjection->battleGetUnitByID(attackerId)->getPosition();
	DamageCache rebirthCache;
	BattleExchangeVariant rebirthExchange;
	rebirthExchange.trackAttack(rebirthProjection->getForUpdate(attackerId),
		rebirthProjection->getForUpdate(sourceId), false, false, rebirthCache, rebirthProjection,
		false, false);
	ASSERT_FALSE(rebirthProjection->battleGetUnitByID(sourceId)->alive());
	const auto spawns = rebirthSpawns(*rebirthProjection);
	ASSERT_EQ(spawns.size(), 1u);
	ASSERT_EQ(rebirthProjection->getProjectedPrimalBurstHits().size(), 1u);
	const auto & burstHit = rebirthProjection->getProjectedPrimalBurstHits().front();
	EXPECT_EQ(burstHit.preHitTarget->unitId(), attackerId);
	EXPECT_EQ(burstHit.preHitTarget->getAvailableHealth(), liveAttackerHealth);
	EXPECT_EQ(burstHit.actualDamage, newHorizonsElementalRebirth::primalBurstDamageBudget(
		spawns.front()->getAvailableHealth()));

	const auto rebirthScore = rebirthExchange.getScore().enemyDamageReduce
		- rebirthExchange.getScore().ourDamageReduce;
	const auto enemies = rebirthProjection->battleGetUnitsIf([&](const battle::Unit * unit)
	{
		return unit && unit->alive() && !rebirthProjection->battleMatchOwner(spawns.front(), unit);
	});
	ASSERT_FALSE(enemies.empty());
	const auto fullSpawnHP = static_cast<int64_t>(spawns.front()->getCount()) * spawns.front()->getMaxHealth();
	ASSERT_GT(fullSpawnHP, 0);
	const float expectedReplacementValue = static_cast<float>(rebirthCache.getOriginalDamage(
		spawns.front(), enemies.front(), rebirthProjection)) * static_cast<float>(spawns.front()->getAvailableHealth())
		/ static_cast<float>(fullSpawnHP);
	const float expectedBurstValue = AttackPossibility::calculateDamageReduce(nullptr,
		burstHit.preHitTarget.get(), static_cast<uint64_t>(burstHit.actualDamage), rebirthCache, rebirthProjection);

	// A child branch must retain the owned pre-hit snapshot and burst ledger entry.
	auto childProjection = std::make_shared<HypotheticBattle>(environment.get(), rebirthProjection);
	ASSERT_EQ(childProjection->getProjectedPrimalBurstHits().size(), 1u);
	EXPECT_EQ(childProjection->getProjectedPrimalBurstHits().front().preHitTarget->getAvailableHealth(),
		liveAttackerHealth);

	confluxHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(REBIRTH_SKILL)),
		MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	auto baselineProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	baselineProjection->moveUnit(attackerId, adjacentPosition);
	DamageCache baselineCache;
	BattleExchangeVariant baselineExchange;
	baselineExchange.trackAttack(baselineProjection->getForUpdate(attackerId),
		baselineProjection->getForUpdate(sourceId), false, false, baselineCache, baselineProjection,
		false, false);
	const auto baselineScore = baselineExchange.getScore().enemyDamageReduce
		- baselineExchange.getScore().ourDamageReduce;
	EXPECT_TRUE(baselineProjection->getElementalRebirthSpawnUnitIds().empty());
	EXPECT_TRUE(baselineProjection->getProjectedPrimalBurstHits().empty());
	EXPECT_GT(expectedReplacementValue, 0.0f);
	EXPECT_GT(expectedBurstValue, 0.0f);
	EXPECT_NEAR(rebirthScore - baselineScore, expectedReplacementValue + expectedBurstValue,
		std::max(1.0f, (expectedReplacementValue + expectedBurstValue) * 0.02f))
		<< "The detached exchange values both the temporary replacement and its magical collateral";
	EXPECT_EQ(source->getAvailableHealth(), liveSourceHealth);
	EXPECT_EQ(opposingPikemen->getAvailableHealth(), liveAttackerHealth);
}

namespace
{
struct ArmyDenialCount { size_t count = 0; };
class ArmyDenialTarget final : public ILogTarget
{
	std::weak_ptr<ArmyDenialCount> count;
public:
	explicit ArmyDenialTarget(std::weak_ptr<ArmyDenialCount> value) : count(std::move(value)) {}
	void write(const LogRecord & record) override
	{
		if(record.message == "FIXME: battleGetArmyObject access check!")
			if(const auto value = count.lock()) ++value->count;
	}
};
class ArmyDenialCapture
{
	CLogger * logger = CLogger::getGlobalLogger();
	ELogLevel::ELogLevel previous = logger->getLevel();
	std::shared_ptr<ArmyDenialCount> count = std::make_shared<ArmyDenialCount>();
public:
	ArmyDenialCapture()
	{
		logger->setLevel(ELogLevel::ERROR);
		logger->addTarget(std::make_unique<ArmyDenialTarget>(count));
	}
	~ArmyDenialCapture() { count.reset(); logger->setLevel(previous); }
	size_t denials() const { return count->count; }
};
template<class Value> std::vector<std::byte> rebirthVisibilityWire(const Value & value)
{
	CMemorySerializer writer;
	writer.oser & value;
	return writer.extractBuffer();
}
std::string rebirthVisibilityState(const CBattleInfoCallback & view)
{
	JsonNode result;
	for(const auto * unit : view.battleGetAllUnits(false))
	{
		auto & row = result[std::to_string(unit->unitId())];
		row["state"] = unit->acquireState()->save();
		const auto bonuses = unit->getUnstackedBonuses(Selector::all);
		for(const auto & bonus : *bonuses)
			row["bonuses"].Vector().push_back(bonus->toJsonNode());
	}
	return result.toString();
}
}

class ElementalRebirthVisibilityAITest : public NewHorizonsElementalRebirthAITest
{
protected:
	std::shared_ptr<HypotheticBattle> visibleParent, changed, untouched;
	std::optional<newHorizonsElementalRebirth::DeathSnapshot> visibleSnapshot;
	void SetUp() override
	{
		NewHorizonsElementalRebirthAITest::SetUp();
		useRevisedOrders = true;
	}
	void exchangeFrom(PlayerColor observer, bool lethal, bool known)
	{
		auto liveView = std::make_shared<CPlayerBattleCallback>(battle(), observer);
		ASSERT_EQ(liveView->battleGetMySide(), known ? BattleSide::DEFENDER : BattleSide::ATTACKER);
		ASSERT_TRUE(heroCommands::isRevisedRules(liveView->getBattle()->getHeroCommandRules()));
		const auto policy = liveView->battleGetVisibleHeroPolicy(BattleSide::DEFENDER);
		ASSERT_EQ(policy.kind(), known ? VisibleHeroPolicy::Knowledge::VISIBLE_HERO
			: VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
		if(!known)
		{
			ASSERT_EQ(liveView->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
			ArmyDenialCapture guard;
			EXPECT_EQ(liveView->battleGetArmyObject(BattleSide::DEFENDER), nullptr);
			EXPECT_EQ(guard.denials(), 1u); // The guard must remain active and diagnostic.
		}
		for(const auto * unit : battle()->battleGetAllUnits(false)) (void)unit->ableToRetaliate();
		visibleParent = std::make_shared<HypotheticBattle>(environment.get(), liveView);
		changed = std::make_shared<HypotheticBattle>(environment.get(), visibleParent);
		untouched = std::make_shared<HypotheticBattle>(environment.get(), visibleParent);
		EXPECT_EQ(visibleParent->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), policy.kind());
		EXPECT_EQ(changed->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), policy.kind());
		EXPECT_EQ(visibleParent->captureElementalRebirthSource(*source).has_value(), known);
		const auto parentBefore = rebirthVisibilityState(*visibleParent);
		const auto siblingBefore = rebirthVisibilityState(*untouched);
		const auto worldBefore = gameState()->saveToMemory();
		const auto authorityRng = rebirthVisibilityWire(*gameHandler->randomizer);
		const auto fixtureRng = rebirthVisibilityWire(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto defaultRng = rebirthVisibilityWire(CRandomGenerator::getDefault());
		auto actor = changed->getForUpdate(opposingPikemen->unitId());
		auto victim = changed->getForUpdate(source->unitId());
		ArmyDenialCapture calls;
		visibleSnapshot = changed->captureElementalRebirthSource(*victim);
		RecordProperty("capturedDeathPolicy", visibleSnapshot ? "1" : "0");
		if(!known && lethal) EXPECT_FALSE(visibleSnapshot) << "Hidden is unknown, never a revealed private Rebirth rank";
		DamageCache cache;
		const auto damage = cache.getDamage(actor.get(), victim.get(), changed);
		if(lethal) ASSERT_GE(damage, victim->getAvailableHealth());
		else ASSERT_LT(damage, victim->getAvailableHealth());
		BattleExchangeVariant exchange;
		// Same ordinary applied future-exchange path as the retained Rebirth AI test.
		exchange.trackAttack(actor, victim, false, false, cache, changed, false, false);
		RecordProperty("deniedArmyQueriesDuringExchange", std::to_string(calls.denials()));
		RecordProperty("projectedRebirthSpawns", std::to_string(changed->getElementalRebirthSpawnUnitIds().size()));
		EXPECT_EQ(calls.denials(), 0u);
		EXPECT_EQ(victim->alive(), !lethal);
		EXPECT_EQ(changed->getElementalRebirthSpawnUnitIds().size(), known && lethal ? 1u : 0u);
		EXPECT_EQ(liveView->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), policy.kind());
		EXPECT_EQ(rebirthVisibilityState(*visibleParent), parentBefore);
		EXPECT_EQ(rebirthVisibilityState(*untouched), siblingBefore);
		EXPECT_EQ(gameState()->saveToMemory(), worldBefore);
		EXPECT_EQ(rebirthVisibilityWire(*gameHandler->randomizer), authorityRng);
		EXPECT_EQ(rebirthVisibilityWire(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
		EXPECT_EQ(rebirthVisibilityWire(CRandomGenerator::getDefault()), defaultRng);
	}
};

TEST_F(ElementalRebirthVisibilityAITest, OpposingPlayerLethalExchangeDoesNotInferHiddenRebirth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(exchangeFrom(battle()->getSidePlayer(BattleSide::ATTACKER), true, false));
}

TEST_F(ElementalRebirthVisibilityAITest, NeutralSideLethalExchangeRetainsUnknownPolicyAndArmyDenial)
{
	neutralObserver = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(battle()->getSidePlayer(BattleSide::ATTACKER), PlayerColor::NEUTRAL);
	// A real neutral side binding, not an invalid-player callback or full treasury replay.
	ASSERT_NO_FATAL_FAILURE(exchangeFrom(PlayerColor::NEUTRAL, true, false));
}

TEST_F(ElementalRebirthVisibilityAITest, NonlethalOpposingExchangeCannotTriggerRebirth)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 1000));
	ASSERT_NO_FATAL_FAILURE(exchangeFrom(battle()->getSidePlayer(BattleSide::ATTACKER), false, false));
}

TEST_F(ElementalRebirthVisibilityAITest, HiddenHeroWithoutRebirthKeepsOrdinaryLethalExchange)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	confluxHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(REBIRTH_SKILL)), 0, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(exchangeFrom(battle()->getSidePlayer(BattleSide::ATTACKER), true, false));
}

TEST_F(ElementalRebirthVisibilityAITest, KnownSideProjectionAndNativeSpawnRetainArmyHealth)
{
	armyHealthBonus = 17;
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 17, nullptr, false, 1000));
	ASSERT_NO_FATAL_FAILURE(exchangeFrom(battle()->getSidePlayer(BattleSide::DEFENDER), true, true));
	ASSERT_TRUE(visibleSnapshot);
	const auto projected = rebirthSpawns(*changed);
	ASSERT_EQ(projected.size(), 1u);
	const auto * predicted = projected.front();
	const auto expectedMax = newHorizonsElementalRebirth::effectiveSummonMaxHP(confluxHero,
		predicted->creatureId(), confluxHero->getOwner(), BattleSide::DEFENDER);
	ASSERT_GT(expectedMax, predicted->unitType()->getMaxHealth());
	EXPECT_EQ(predicted->getMaxHealth(), expectedMax);
	const auto targetHP = newHorizonsElementalRebirth::targetHP(*visibleSnapshot, predicted->creatureId(), *changed);
	EXPECT_EQ(predicted->getAvailableHealth(), targetHP);
	// Only this legally known-policy case executes native authority for HP parity.
	BattleStackAttacked hit;
	hit.attackerID = opposingPikemen->unitId();
	hit.stackAttacked = source->unitId();
	hit.damageAmount = source->getAvailableHealth();
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), source->acquireState());
	ASSERT_TRUE(hit.killed());
	StacksInjured packet;
	packet.battleID = BattleID(0);
	packet.stacks.push_back(std::move(hit));
	gameHandler->sendAndApply(packet);
	const auto outputs = battle()->battleGetUnitsIf([](const battle::Unit * unit)
		{ return unit->alive() && unit->isSummoned(); });
	ASSERT_EQ(outputs.size(), 1u);
	const auto * actual = outputs.front();
	EXPECT_EQ(actual->getAvailableHealth(), predicted->getAvailableHealth());
	EXPECT_EQ(actual->getRebirthOriginalAggregateHP(), predicted->getRebirthOriginalAggregateHP());
	EXPECT_EQ(actual->getMaxHealth(), newHorizonsElementalRebirth::effectiveSummonMaxHP(confluxHero,
		actual->creatureId(), confluxHero->getOwner(), BattleSide::DEFENDER));
	// Native and deterministic projection may choose different legal elemental types.
	EXPECT_EQ(actual->getAvailableHealth(), newHorizonsElementalRebirth::targetHP(
		*visibleSnapshot, actual->creatureId(), *battle()));
}

TEST_F(NewHorizonsElementalRebirthAITest, PrimalBurstLethalBinderReleasesOnlyItsHoldAndIsolatesBranchesAndRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, 12000, nullptr, true, 1));
	attackerSideHero->setHeroType(heroType("core:fafner"));
	const auto freeNagaAnchor = [&](bool adjacent)
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex anchor(index);
			if(!accessibility.accessibleForMovementEndpoint(anchor, true, BattleSide::ATTACKER))
				continue;
			const auto & footprint = battle::Unit::getHexes(anchor, true, BattleSide::ATTACKER);
			const bool touchesSource = std::ranges::any_of(footprint, [&](const auto hex)
				{ return vstd::contains(source->getSurroundingHexes(), hex); });
			if(touchesSource == adjacent)
				return anchor;
		}
		return BattleHex(BattleHex::INVALID);
	};
	const auto near = freeNagaAnchor(true);
	ASSERT_TRUE(near.isValid());
	auto * binder = addStack(BattleSide::ATTACKER, creature("core:naga"), near, 1);
	const auto far = freeNagaAnchor(false);
	ASSERT_TRUE(far.isValid());
	auto * otherBinder = addStack(BattleSide::ATTACKER, creature("core:nagaQueen"), far, 1);
	ASSERT_TRUE(battle()->isMeleeAttackPossible(binder, source));
	const auto hostileIds = newHorizonsElementalRebirth::adjacentHostileUnitIds(*battle(), *source);
	ASSERT_TRUE(vstd::contains(hostileIds, binder->unitId()));
	ASSERT_FALSE(vstd::contains(hostileIds, otherBinder->unitId()));
	SetStackEffect holds;
	holds.battleID = BattleID(0);
	for(const auto * naga : {binder, otherBinder})
		holds.toAdd.emplace_back(survivingAlly->unitId(), std::vector<Bonus>{
			newHorizonsSerpentsEmbrace::marker(naga->unitId(), naga->creatureId())});
	gameHandler->sendAndApply(holds);
	const auto liveBefore = std::tuple{source->acquireState()->save(), binder->save(),
		otherBinder->save(), survivingAlly->acquireState()->save()};
	const auto rngBytes = [&]()
	{
		CMemorySerializer authority, fixture, defaults;
		authority.oser & *gameHandler->randomizer;
		fixture.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		defaults.oser & CRandomGenerator::getDefault();
		return std::tuple{authority.extractBuffer(), fixture.extractBuffer(), defaults.extractBuffer()};
	};
	const auto rngBefore = rngBytes();
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto branchState = [&](const HypotheticBattle & branch)
	{
		const auto * victim = branch.battleGetUnitByID(survivingAlly->unitId());
		return std::tuple{branch.battleGetUnitByID(source->unitId())->acquireState()->save(),
			branch.battleGetUnitByID(binder->unitId())->acquireState()->save(),
			branch.battleGetUnitByID(otherBinder->unitId())->acquireState()->save(), victim->acquireState()->save(),
			newHorizonsSerpentsEmbrace::releasePlan(*victim, binder->unitId()).size(),
			newHorizonsSerpentsEmbrace::releasePlan(*victim, otherBinder->unitId()).size(),
			branch.getElementalRebirthSpawnUnitIds(), branch.getProjectedPrimalBurstHits().size()};
	};
	const auto parentBefore = branchState(*parent);
	const auto siblingBefore = branchState(*sibling);
	const auto snapshot = captureAndKillSource(*child, source->unitId());
	ASSERT_TRUE(snapshot.has_value());
	ASSERT_TRUE(snapshot->profile.primalBurst);
	ASSERT_GE(newHorizonsElementalRebirth::primalBurstDamageBudget(
		newHorizonsElementalRebirth::targetHP(*snapshot)), binder->getAvailableHealth());
	ASSERT_TRUE(child->projectElementalRebirth(child->battleGetUnitByID(source->unitId()),
		*snapshot, true, false, false));
	ASSERT_EQ(child->getProjectedPrimalBurstHits().size(), 1u);
	const auto & hit = child->getProjectedPrimalBurstHits().front();
	ASSERT_EQ(hit.preHitTarget->unitId(), binder->unitId());
	ASSERT_GT(hit.actualDamage, 0);
	EXPECT_EQ(hit.actualDamage, hit.preHitTarget->getAvailableHealth());
	EXPECT_FALSE(child->battleGetUnitByID(binder->unitId())->alive());
	const auto * victim = child->battleGetUnitByID(survivingAlly->unitId());
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::releasePlan(*victim, binder->unitId()).empty());
	EXPECT_EQ(newHorizonsSerpentsEmbrace::releasePlan(*victim, otherBinder->unitId()).size(), 1u);
	EXPECT_TRUE(child->battleGetUnitByID(otherBinder->unitId())->alive());
	EXPECT_EQ(branchState(*parent), parentBefore);
	EXPECT_EQ(branchState(*sibling), siblingBefore);
	EXPECT_EQ((std::tuple{source->acquireState()->save(), binder->save(),
		otherBinder->save(), survivingAlly->acquireState()->save()}), liveBefore);
	EXPECT_EQ(newHorizonsSerpentsEmbrace::releasePlan(*survivingAlly, binder->unitId()).size(), 1u);
	EXPECT_EQ(newHorizonsSerpentsEmbrace::releasePlan(*survivingAlly, otherBinder->unitId()).size(), 1u);
	EXPECT_EQ(rngBytes(), rngBefore);
}
