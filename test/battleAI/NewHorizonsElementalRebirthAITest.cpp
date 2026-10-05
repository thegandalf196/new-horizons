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
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
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

		startBattle();
		source = unitAt(*battle(), BattleSide::DEFENDER, SlotID(0));
		survivingAlly = unitAt(*battle(), BattleSide::DEFENDER, SlotID(1));
		opposingPikemen = unitAt(*battle(), BattleSide::ATTACKER, SlotID(0));
		ASSERT_NE(source, nullptr);
		ASSERT_NE(survivingAlly, nullptr);
		ASSERT_NE(opposingPikemen, nullptr);
		ASSERT_GT(source->getBattleStartMaximumAggregateHP(), 0);
		beginCombat();

		callback = std::make_shared<ElementalRebirthCallback>(battle()->getSidePlayer(BattleSide::DEFENDER));
		callback->onBattleStarted(battle());
		ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetFightingHero(BattleSide::DEFENDER), confluxHero);
		environment = std::make_shared<ElementalRebirthEnvironment>(gameState());
	}
};

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
