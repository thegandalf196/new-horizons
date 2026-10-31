/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../server/battles/BattleActionProcessor.h"
#include "../../../server/battles/BattleProcessor.h"
#include <utility>
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/battle/NewHorizonsGnollHarriers.h"
#include "../../../lib/battle/NewHorizonsToxicSting.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../../server/battles/BattleToxicSting.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/battles/BattleVoluntaryMovementHistory.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/PotentialTargets.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../../AI/BattleAI/VisibleCandidateWorld.h"
#include "../../../AI/BattleAI/ProjectedBestAction.h"
#include "../../../AI/BattleAI/GnollHarriersProjection.h"
#include "../../../AI/BattleAI/ToxicStingValue.h"
#include "../../../AI/BattleAI/PhysicalPoisonValue.h"
#include "../../../AI/BattleAI/AttackAbilityPrefix.h"
#include "../../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../../lib/battle/CreatureAbilityChance.h"
#include <vcmi/Environment.h>
#endif
namespace
{
namespace sting = newHorizonsToxicSting;
namespace harriers = newHorizonsGnollHarriers;
class StingHarriersServer : public RecordingGameServer
{
	struct PoisonUpdate
	{
		BattleID battle;
		uint32_t target;
		int64_t base;
		int32_t source;
	};
	std::vector<PoisonUpdate> pendingPoisonUpdates;
public:
	uint32_t actorId = 0;
	int poisonApplications = 0;
	int poisonSource = -1;
	uint32_t poisonTarget = 0;
	int poisonStateSnapshots = 0;
	int64_t poisonBase = 0;
	int counterMultiplier = 0;
	std::vector<std::string> ordered;
	void applyPack(CPackForClient & pack) override
	{
		const auto precedingPoisonUpdates = std::exchange(pendingPoisonUpdates, {});
		if(const auto * cast = dynamic_cast<const BattleSpellCast *>(&pack))
			ordered.push_back(cast->spellID == SpellID::PARALYZE ? "paralysis" : "spell");
		if(const auto * changes = dynamic_cast<const BattleUnitsChanged *>(&pack))
			for(const auto & change : changes->changedStacks)
				if(change.operation == UnitChanges::EOperation::UPDATE && change.data.isStruct() && change.data["state"].isStruct()
					&& change.data["state"].Struct().contains("physicalPoisonActivationsRemaining")
					&& change.data["state"]["physicalPoisonActivationsRemaining"].Integer() == 3)
				{
					++poisonStateSnapshots;
					pendingPoisonUpdates.push_back({changes->battleID, change.id,
						change.data["state"]["physicalPoisonBaseDamage"].Integer(),
						static_cast<int32_t>(change.data["state"]["physicalPoisonSourceStackId"].Integer())});
				}
		if(const auto * message = dynamic_cast<const BattleLogMessage *>(&pack))
			for(const auto & line : message->lines)
				if(line.toString(LIBRARY->staticTexts()).starts_with("Toxic Sting poisons "))
				{
					++poisonApplications; ordered.push_back("poison");
					EXPECT_EQ(precedingPoisonUpdates.size(), 1u) << "Toxic Sting requires its immediately preceding native UPDATE";
					if(precedingPoisonUpdates.size() != 1) continue;
					const auto & receipt = precedingPoisonUpdates.front();
					EXPECT_EQ(receipt.battle, message->battleID); EXPECT_GT(receipt.base, 0); EXPECT_GE(receipt.source, 0);
					const auto * combat = gameState->getBattle(message->battleID);
					const auto * target = combat ? combat->battleGetStackByID(receipt.target, false) : nullptr;
					EXPECT_NE(target, nullptr);
					if(!target) continue;
					EXPECT_EQ(target->physicalPoisonActivationsRemaining, 3);
					EXPECT_EQ(target->physicalPoisonBaseDamage, receipt.base);
					EXPECT_EQ(target->physicalPoisonSourceStackId, receipt.source);
					poisonBase = receipt.base; poisonSource = receipt.source; poisonTarget = receipt.target;
				}
		if(const auto * attack = dynamic_cast<const BattleAttack *>(&pack); attack && attack->counter() && attack->stackAttacking == actorId)
		{
			const auto * combat = gameState->getBattle(attack->battleID);
			const auto * actor = combat->battleGetUnitByID(actorId);
			const auto * victim = attack->bsa.empty() ? nullptr : combat->battleGetUnitByID(attack->bsa.front().stackAttacked);
			BattleAttackInfo info(actor, victim, 0, false); info.retaliation = true;
			counterMultiplier = newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*combat, info);
		}
		RecordingGameServer::applyPack(pack);
	}
};
class NewHorizonsStingHarriersTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	StingHarriersServer observed;
	int captureMode = 0;
	int chance = 100; // Deterministic native witness; the experimental profile is20, tested separately.
	void TearDown() override
	{
		gameHandler.reset(); observed.gameState.reset(); HeroCommandFixture::TearDown();
	}
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true; HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		policy["toxicStingChancePercent"].Integer() = captureMode == 2 ? 0 : chance;
		policy["gnollHarriersDamagePercent"].Integer() = captureMode == 2 ? 0 : 20;
		if(captureMode == 1)
			for(const char * key : {"toxicStingChancePercent", "gnollHarriersDamagePercent"}) policy.Struct().erase(key);
		rules.setOverrideFlag(true); map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(bool drakon = false, bool upgraded = false, bool victimFirst = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(drakon ? "core:drakon" : "core:synca")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName(drakon ? (upgraded ? "core:gnollMarauder" : "core:gnoll")
				: (upgraded ? "core:scorpicore" : "core:manticore")), 100}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 200}});
		startWithMap(std::move(builder)); server.gameState = gameState(); observed.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(observed, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0)); defenderSideHero = findHeroByOwner(PlayerColor(1)); startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0)) (unit->unitSide() == BattleSide::ATTACKER ? actor : victim) = unit.get();
		ASSERT_NE(actor, nullptr); ASSERT_NE(victim, nullptr); observed.actorId = actor->unitId();
		actor->setPosition(BattleHex(3, 5)); victim->setPosition(BattleHex(4, 5));
		for(auto * unit : {actor, victim})
		{
			for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			{
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
					BonusSource::OTHER, (unit == victim) == victimFirst ? 1000 : 10, BonusSourceID(), BonusSubtypeID(), bound));
				for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
					unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
						BonusSource::OTHER, unit == actor ? 2 : 1, BonusSourceID(), subtype, bound));
			}
		}
	}
	void giveVictimGuardianPool(int32_t pool)
	{
		Bonus marker(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
			BonusSource::SPELL_EFFECT, pool, BonusSourceID(SpellID(SpellID::HASTE)));
		marker.turnsRemain = 3;
		SetStackEffect effects; effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(victim->unitId(), std::vector<Bonus>{marker});
		gameHandler->sendAndApply(effects);
	}
	void commitHistory(bool voluntary = true)
	{
		battleVoluntaryMovementHistory::record(*gameHandler, *battle(), *victim, voluntary, BattleHex(5, 5), victim->getPosition());
	}
};
#if ENABLE_BATTLE_AI
class HarriersEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit HarriersEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST_F(NewHorizonsStingHarriersTest, SyncaUsesCapturedTwentyPercentAndOrdinaryRetaliationWithExistingPoisonMath)
{
	chance = 20; prepare(); BattleAttackInfo hit(actor, victim, 0, false); hit.retaliation = true;
	const auto plan = sting::prepare(*battle(), hit, 19); ASSERT_TRUE(plan);
	EXPECT_EQ(plan->chancePercent, 20); EXPECT_EQ(plan->baseDamage, 4);
	auto state = victim->acquireState(); ASSERT_TRUE(sting::apply(*plan, *state));
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(state.get()), 4);
	newHorizonsBulwark::advancePhysicalPoison(state.get());
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(state.get()), 6);
	EXPECT_TRUE(sting::apply(*plan, *state)); EXPECT_EQ(state->physicalPoisonActivationsRemaining, 3);
	auto weaker = *plan; weaker.baseDamage = 3;
	EXPECT_FALSE(sting::apply(weaker, *state)); EXPECT_EQ(state->physicalPoisonBaseDamage, 4);
	newHorizonsBulwark::advancePhysicalPoison(state.get()); newHorizonsBulwark::advancePhysicalPoison(state.get());
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(state.get()), 8);
	newHorizonsBulwark::clearPhysicalPoison(state.get()); EXPECT_EQ(state->physicalPoisonActivationsRemaining, 0);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaRejectsNonlivingZeroBodyAndNonordinaryPrimaryFacts)
{
	prepare(); BattleAttackInfo hit(actor, victim, 0, false);
	EXPECT_FALSE(sting::prepare(*battle(), hit, 0));
	for(int exclusion = 0; exclusion < 6; ++exclusion)
	{
		auto invalid = hit;
		if(exclusion == 0) invalid.shooting = true;
		if(exclusion == 1) invalid.physicalDamage = false;
		if(exclusion == 2) invalid.secondaryAttack = true;
		if(exclusion == 3) invalid.bracePreemptive = true;
		if(exclusion == 4) invalid.preemptiveDamagePercent = 50;
		if(exclusion == 5) invalid.cleaveDamagePercent = 50;
		EXPECT_FALSE(sting::prepare(*battle(), invalid, 4));
	}
	for(const auto type : {BonusType::UNDEAD, BonusType::NON_LIVING, BonusType::MECHANICAL, BonusType::SIEGE_WEAPON})
	{
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
		EXPECT_FALSE(sting::prepare(*battle(), hit, 4));
		victim->removeBonusesRecursive(Selector::type()(type));
	}
}
TEST_F(NewHorizonsStingHarriersTest, NativeScorpicoreParalysisPrecedesPoisonWithoutAnAdditionalCastOrAction)
{
	prepare(false, true);
	ASSERT_TRUE(actor->hasBonus(Selector::typeSubtype(BonusType::SPELL_AFTER_ATTACK, BonusSubtypeID(SpellID(SpellID::PARALYZE)))));
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_AFTER_ATTACK,
		BonusSource::OTHER, 80, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto initialActions = observed.startedActions.size(); ASSERT_TRUE(attack(actor, victim->getPosition()));
	ASSERT_EQ(observed.poisonApplications, 1); EXPECT_EQ(observed.poisonSource, actor->unitId());
	ASSERT_FALSE(observed.attacks.empty());
	const auto primary = std::ranges::find(observed.attacks.front().bsa, victim->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(primary, observed.attacks.front().bsa.end());
	EXPECT_EQ(observed.poisonBase, std::max<int64_t>(1, primary->damageAmount / 4));
	ASSERT_EQ(observed.ordered.size(), 2u); EXPECT_EQ(observed.ordered[0], "paralysis"); EXPECT_EQ(observed.ordered[1], "poison");
	EXPECT_EQ(observed.startedActions.size(), initialActions + 1);
}
TEST_F(NewHorizonsStingHarriersTest, NativeShieldOnlyHitDoesNotApplyPoison)
{
	prepare(); giveVictimGuardianPool(100000);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(victim->hasBonusOfType(BonusType::GUARDIAN_SPIRIT));
	ASSERT_EQ(victim->guardianSpiritHitPoints, 100000); ASSERT_EQ(victim->guardianSpiritRoundsRemaining, 3);
	const auto bodyBefore = victim->health.getCreatureHealthAvailable();
	ASSERT_TRUE(attack(actor, victim->getPosition())); ASSERT_FALSE(observed.attacks.empty());
	const auto primary = std::ranges::find(observed.attacks.front().bsa, victim->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(primary, observed.attacks.front().bsa.end()); EXPECT_EQ(primary->damageAmount, 0);
	EXPECT_EQ(victim->health.getCreatureHealthAvailable(), bodyBefore);
	EXPECT_EQ(observed.poisonApplications, 0); EXPECT_EQ(victim->physicalPoisonBaseDamage, 0);
}
TEST_F(NewHorizonsStingHarriersTest, NativePrimaryReceiptExcludesTemporaryHealthWithoutSubtractingGuardianTwice)
{
	prepare(); victim->health.addTemporaryHitPoints(9); giveVictimGuardianPool(13);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_EQ(victim->guardianSpiritHitPoints, 13); ASSERT_EQ(victim->guardianSpiritRoundsRemaining, 3);
	ASSERT_EQ(victim->health.getTemporaryHitPoints(), 9);
	ASSERT_TRUE(attack(actor, victim->getPosition()));
	ASSERT_EQ(observed.poisonApplications, 1) << "Full Poison snapshots: " << observed.poisonStateSnapshots;
	EXPECT_EQ(observed.poisonTarget, victim->unitId()); EXPECT_EQ(observed.poisonSource, actor->unitId());
	ASSERT_FALSE(observed.attacks.empty());
	const auto primary = std::ranges::find(observed.attacks.front().bsa, victim->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(primary, observed.attacks.front().bsa.end()); ASSERT_GT(primary->damageAmount, 9);
	EXPECT_NE(primary->flags & BattleStackAttacked::GUARDIAN_SPIRIT_EXHAUSTED, 0u);
	EXPECT_EQ(victim->guardianSpiritHitPoints, 0); EXPECT_EQ(victim->health.getTemporaryHitPoints(), 0);
	EXPECT_EQ(observed.poisonBase, std::max<int64_t>(1, (primary->damageAmount - 9) / 4));
}
TEST_F(NewHorizonsStingHarriersTest, PrehitBodyReceiptSubtractsTemporaryPoolRatherThanUsingNetPostRevivalHealth)
{
	prepare(); auto state = victim->acquireState();
	state->health.addTemporaryHitPoints(9); victim->load(state->save());
	ASSERT_EQ(victim->health.getTemporaryHitPoints(), 9);
	BattleStackAttacked hit; hit.stackAttacked = victim->unitId(); hit.attackerID = actor->unitId(); hit.damageAmount = 13;
	const auto plan = battleToxicSting::prepare(*battle(), BattleAttackInfo(actor, victim, 0, false), {hit});
	ASSERT_TRUE(plan); EXPECT_EQ(plan->baseDamage, 1);
	hit.damageAmount = 9;
	EXPECT_FALSE(battleToxicSting::prepare(*battle(), BattleAttackInfo(actor, victim, 0, false), {hit}));
}
TEST_F(NewHorizonsStingHarriersTest, NativeWalkingCommitArmsHarriersBeforeOrdinaryRetaliation)
{
	prepare(true, true, true); victim->setPosition(BattleHex(6, 5)); beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), victim);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(victim, actor, BattleHex(4, 5))));
	EXPECT_EQ(observed.counterMultiplier, 120);
	EXPECT_TRUE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, NativeFlyingMoveRecordsTheSameHistoryWithoutRequiringAnAttack)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::FLYING, BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), victim);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeMove(victim, BattleHex(5, 5))));
	EXPECT_TRUE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_EQ(harriers::damagePercent(*battle(), BattleAttackInfo(actor, victim, 0, false)), 20);
}
TEST_F(NewHorizonsStingHarriersTest, NativeAutomaticMoveHasNoVoluntaryHistory)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5)); beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), victim);
	BattleActionProcessor automatic(gameHandler->battles.get(), gameHandler.get());
	ASSERT_TRUE(automatic.makeAutomaticBattleAction(*battle(), BattleAction::makeMove(victim, BattleHex(5, 5))));
	EXPECT_EQ(victim->getPosition(), BattleHex(5, 5));
	EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, NativeHeroTeleportDoesNotCreateMovementHistory)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5));
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::TELEPORT); setTestSpellPointTotal(defenderSideHero, 100);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), victim);
	BattleAction teleport; teleport.actionType = EActionType::HERO_SPELL;
	teleport.side = BattleSide::DEFENDER; teleport.spell = SpellID::TELEPORT;
	teleport.aimToUnit(victim); teleport.aimToHex(BattleHex(5, 5));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), teleport));
	EXPECT_EQ(victim->getPosition(), BattleHex(5, 5));
	EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, LatestBegunActivationHistorySurvivesRoundsAndContinuationsButNotDeferredWaitEntry)
{
	prepare(true); EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory))); commitHistory();
	BattleNextRound next; next.battleID = BattleID(0); gameHandler->sendAndApply(next);
	EXPECT_TRUE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
	for(const auto reason : {BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST,
		BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::PURSUIT_CONTINUATION,
		BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION, BattleUnitTurnReason::CRISIS_RESUME,
		BattleUnitTurnReason::HERO_COMMAND, BattleUnitTurnReason::MASTER_GATE_CONTINUATION,
		BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION})
	{
		EXPECT_FALSE(battle()->battleBeginsActivation(victim, reason)); battle()->nextTurn(victim->unitId(), reason);
		EXPECT_TRUE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
	}
	victim->afterWait(); ASSERT_TRUE(victim->waited());
	// Pin the actual shared helper: deferred Wait returns via TURN_QUEUE and is genuine.
	ASSERT_TRUE(battle()->battleBeginsActivation(victim, BattleUnitTurnReason::TURN_QUEUE));
	battle()->nextTurn(victim->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, ForcedMovementAndTimeStopDoNotCreateOrEraseCommittedVoluntaryHistory)
{
	prepare(true); commitHistory(false); EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
	commitHistory(); ASSERT_TRUE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
	commitHistory(false); EXPECT_TRUE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_FALSE(battle()->battleBeginsActivation(victim, BattleUnitTurnReason::AUTOMATIC_ACTION));
	battle()->nextTurn(victim->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION);
	EXPECT_TRUE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, MoraleReducedExtraAndGenuineIncapacitatedActivationReplacePriorMovementHistory)
{
	prepare(true);
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NOT_ACTIVE, BonusSource::OTHER, 1, BonusSourceID()));
	for(const auto reason : {BattleUnitTurnReason::MORALE, BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION,
		BattleUnitTurnReason::AUTOMATIC_ACTION})
	{
		commitHistory(); ASSERT_TRUE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
		ASSERT_TRUE(battle()->battleBeginsActivation(victim, reason)); battle()->nextTurn(victim->unitId(), reason);
		EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
	}
}
TEST_F(NewHorizonsStingHarriersTest, HarriersRejectsReactionsSpellsAndCollateralButIncludesOrdinaryRetaliation)
{
	prepare(true); commitHistory(); BattleAttackInfo hit(actor, victim, 0, false); hit.retaliation = true;
	EXPECT_EQ(harriers::damagePercent(*battle(), hit), 20);
	for(int exclusion = 0; exclusion < 6; ++exclusion)
	{
		auto invalid = hit;
		if(exclusion == 0) invalid.shooting = true;
		if(exclusion == 1) invalid.physicalDamage = false;
		if(exclusion == 2) invalid.secondaryAttack = true;
		if(exclusion == 3) invalid.bracePreemptive = true;
		if(exclusion == 4) invalid.preemptiveDamagePercent = 50;
		if(exclusion == 5) invalid.cleaveDamagePercent = 50;
		EXPECT_EQ(harriers::damagePercent(*battle(), invalid), 0);
	}
}
TEST_F(NewHorizonsStingHarriersTest, MovementHistoryDescriptorRoundtripRetainsExactNeutralShape)
{
	prepare(true); commitHistory();
	const auto before = victim->getUnstackedBonuses(CSelector(harriers::isMovementHistory))->toJsonNode();
	CMemorySerializer current;
	EXPECT_THROW(battleStartFixture::writeDescriptors(*battle(), current), std::runtime_error);
	EXPECT_TRUE(current.extractBuffer().empty());
	constexpr auto version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	CMemorySerializer wire;
	wire.oser.version = version;
	ASSERT_NO_THROW(battleStartFixture::writeDescriptors(*battle(), wire));
	wire.iser.version = version; wire.iser.cb = gameState().get();
	auto restored = std::make_unique<BattleInfo>(gameState().get());
	ASSERT_NO_THROW(wire.iser & *restored);
	const auto * loaded = restored->getStack(victim->unitId(), false); ASSERT_NE(loaded, nullptr);
	BonusList decoded;
	loaded->getExportedBonusList().getBonuses(decoded, CSelector(harriers::isMovementHistory));
	EXPECT_EQ(decoded.toJsonNode(), before);
	ASSERT_NO_THROW(restored->localInit());
	EXPECT_EQ(loaded->getUnstackedBonuses(CSelector(harriers::isMovementHistory))->toJsonNode(), before);
	auto malformed = *victim->getFirstBonus(CSelector(harriers::isMovementHistory)); malformed.val = 2;
	EXPECT_THROW(harriers::isMovementHistory(&malformed), std::runtime_error);
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsStingHarriersTest, MaterializedLazyChildAndSiblingOwnTheirHistoryWhenParentOrLiveSourceExpires)
{
	prepare(true); commitHistory(); HarriersEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto original = parent->getForUpdate(victim->unitId());
	auto child = std::make_shared<HypotheticBattle>(&env, parent); auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	auto copied = child->getForUpdate(victim->unitId()); auto untouched = sibling->getForUpdate(victim->unitId());
	original->removeUnitBonus(CSelector(harriers::isMovementHistory));
	victim->removeBonusesRecursive(CSelector(harriers::isMovementHistory));
	EXPECT_TRUE(copied->hasBonus(CSelector(harriers::isMovementHistory)));
	copied->removeUnitBonus(CSelector(harriers::isMovementHistory));
	EXPECT_FALSE(copied->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_TRUE(untouched->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_FALSE(original->hasBonus(CSelector(harriers::isMovementHistory)));
}
#endif
class HistoricalStingHarriersTest : public NewHorizonsStingHarriersTest, public ::testing::WithParamInterface<int> {};
TEST_P(HistoricalStingHarriersTest, CapturedAbsenceAndExplicitZeroProduceNeitherPoisonNorMovementHistory)
{
	captureMode = GetParam(); prepare(); beginCombat(); ASSERT_TRUE(attack(actor, victim->getPosition()));
	EXPECT_EQ(observed.poisonApplications, 0);
	EXPECT_FALSE(sting::prepare(*battle(), BattleAttackInfo(actor, victim, 0, false), 10));
	EXPECT_FALSE(harriers::movement(*battle(), *victim, true, BattleHex(5, 5), victim->getPosition()));
}
INSTANTIATE_TEST_SUITE_P(AbsentAndZero, HistoricalStingHarriersTest, ::testing::Values(1, 2));
#if ENABLE_BATTLE_AI
namespace
{
template<class T> auto harriersAIWire(const T & source)
{
	CMemorySerializer memory; memory.oser & source; return memory.extractBuffer();
}
}
TEST_F(NewHorizonsStingHarriersTest, DrakonPotentialTargetsSelectedReplayAndNativeMovingVictimAgree)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5)); beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	HarriersEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto parent = std::make_shared<HypotheticBattle>(&env, known);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	auto selected = std::make_shared<HypotheticBattle>(&env, parent);
	const auto liveActor = actor->save(), liveVictim = victim->save();
	auto * nativeRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRng = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(nativeRng, nullptr); ASSERT_NE(fixtureRng, nullptr);
	const auto nativeBefore = harriersAIWire(*nativeRng), fixtureBefore = harriersAIWire(*fixtureRng);
	DamageCache cache; PotentialTargets targets(parent->battleGetUnitByID(victim->unitId()), cache, parent);
	const auto chosen = std::ranges::find_if(targets.possibleAttacks, [this](const auto & ap)
		{ return ap.from == BattleHex(4, 5) && ap.attack.defender->unitId() == actor->unitId(); });
	ASSERT_NE(chosen, targets.possibleAttacks.end()); ASSERT_TRUE(chosen->validatedVoluntaryApproach);
	ASSERT_NE(chosen->effectPreview, nullptr);
	EXPECT_TRUE(chosen->effectPreview->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	BattleExchangeVariant exchange; exchange.trackAttack(*chosen, selected, cache);
	EXPECT_TRUE(selected->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_EQ(selected->battleGetUnitByID(victim->unitId())->getAvailableHealth(), chosen->attackerState->getAvailableHealth());
	EXPECT_EQ(actor->save(), liveActor); EXPECT_EQ(victim->save(), liveVictim);
	EXPECT_FALSE(sibling->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_EQ(harriersAIWire(*nativeRng), nativeBefore); EXPECT_EQ(harriersAIWire(*fixtureRng), fixtureBefore);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(victim, actor, BattleHex(4, 5))));
	EXPECT_EQ(observed.counterMultiplier, 120);
	EXPECT_EQ(actor->getAvailableHealth(), selected->battleGetUnitByID(actor->unitId())->getAvailableHealth());
	EXPECT_EQ(victim->getAvailableHealth(), selected->battleGetUnitByID(victim->unitId())->getAvailableHealth());
}
TEST_F(NewHorizonsStingHarriersTest, DrakonUnknownMovementFactDoesNotInferHistoryFromChargeDistance)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5)); beginCombat();
	HarriersEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known); DamageCache cache;
	const auto * mover = branch->battleGetUnitByID(victim->unitId()); const auto * target = branch->battleGetUnitByID(actor->unitId());
	const auto unknown = AttackPossibility::evaluate(BattleAttackInfo(mover, target, 2, false), BattleHex(4, 5), cache, branch);
	ASSERT_NE(unknown.effectPreview, nullptr); EXPECT_FALSE(unknown.validatedVoluntaryApproach);
	EXPECT_FALSE(unknown.effectPreview->battleGetUnitByID(mover->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	auto world = std::make_shared<VisibleCandidateWorld>(branch);
	const auto accepted = VisibleCandidateWorld::evaluate(world, mover, target, 2, false, BattleHex(4, 5), cache, 0, true);
	ASSERT_NE(accepted.effectPreview, nullptr); EXPECT_TRUE(accepted.validatedVoluntaryApproach);
	EXPECT_TRUE(accepted.effectPreview->battleGetUnitByID(mover->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_LT(accepted.attackerState->getAvailableHealth(), unknown.attackerState->getAvailableHealth());
}
TEST_F(NewHorizonsStingHarriersTest, DrakonForcedAndTeleportMoveUnitDoNotCreateHistoryButCannotEraseIt)
{
	prepare(true); HarriersEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known);
	branch->moveUnit(victim->unitId(), BattleHex(6, 5));
	EXPECT_FALSE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	branch->projectVoluntaryMovement(victim->unitId(), BattleHex(5, 5), false);
	EXPECT_FALSE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	branch->projectVoluntaryMovement(victim->unitId(), BattleHex(4, 5));
	ASSERT_TRUE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	branch->moveUnit(victim->unitId(), BattleHex(6, 5));
	EXPECT_TRUE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, DrakonProjectedClockMatchesRoundWaitHeroContinuationAndTimeStopBoundaries)
{
	prepare(true); victim->setPosition(BattleHex(6, 5)); HarriersEnvironment env(gameState());
	auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto parent = std::make_shared<HypotheticBattle>(&env, known);
	parent->projectVoluntaryMovement(victim->unitId(), BattleHex(5, 5));
	auto child = std::make_shared<HypotheticBattle>(&env, parent); auto copied = child->getForUpdate(victim->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent); auto untouched = sibling->getForUpdate(victim->unitId());
	parent->nextTurn(victim->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(parent->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_TRUE(copied->hasBonus(CSelector(harriers::isMovementHistory)));
	child->nextRound(); child->nextTurn(copied->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_TRUE(copied->hasBonus(CSelector(harriers::isMovementHistory)));
	copied->afterWait(); ASSERT_TRUE(child->battleBeginsActivation(copied.get(), BattleUnitTurnReason::TURN_QUEUE));
	child->nextTurn(copied->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(copied->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_TRUE(untouched->hasBonus(CSelector(harriers::isMovementHistory)));
	untouched->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_FALSE(sibling->battleBeginsActivation(untouched.get(), BattleUnitTurnReason::AUTOMATIC_ACTION));
	sibling->nextTurn(untouched->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION);
	EXPECT_TRUE(untouched->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, DrakonGenericBestActionAndHeroSnapshotReplayRetainExactMovementMarker)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5)); beginCombat(); HarriersEnvironment env(gameState());
	auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto parent = std::make_shared<HypotheticBattle>(&env, known); DamageCache cache;
	PotentialTargets targets(parent->battleGetUnitByID(victim->unitId()), cache, parent);
	const auto chosen = std::ranges::find_if(targets.possibleAttacks, [this](const auto & ap)
		{ return ap.from == BattleHex(4, 5) && ap.attack.defender->unitId() == actor->unitId(); });
	ASSERT_NE(chosen, targets.possibleAttacks.end()); ASSERT_TRUE(chosen->validatedVoluntaryApproach);
	auto generic = std::make_shared<HypotheticBattle>(&env, parent);
	applyProjectedBestAction(*generic, generic->battleGetUnitByID(victim->unitId()), *chosen);
	EXPECT_TRUE(generic->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	auto hero = std::make_shared<HypotheticBattle>(&env, parent);
	AttackPreviewHitContext::replayOrdinaryStrikeEffects(*hero, *chosen);
	hero->nextTurn(victim->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_TRUE(hero->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_FALSE(parent->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, DrakonExistingStatefulPolicyCacheRequeriesHistoryCreationAndExpiry)
{
	prepare(true); HarriersEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known); DamageCache cache;
	// Materialize branch handles before recording history; a borrowed native pointer is not refreshed by copy-on-write.
	const auto source = branch->getForUpdate(actor->unitId()); const auto target = branch->getForUpdate(victim->unitId());
	const BattleAttackInfo strike(source.get(), target.get(), 0, false);
	ASSERT_EQ(branch->battleGetUnitByID(target->unitId()), target.get());
	ASSERT_FALSE(target->hasBonus(CSelector(harriers::isMovementHistory)));
	ASSERT_EQ(harriers::damagePercent(*branch, strike), 0);
	ASSERT_TRUE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*branch));
	const auto baseline = cache.getDamage(source.get(), target.get(), branch);
	gnollHarriersAI::record(*branch, target->unitId(), BattleHex(5, 5), target->getPosition());
	ASSERT_EQ(branch->battleGetUnitByID(target->unitId()), target.get());
	ASSERT_TRUE(target->hasBonus(CSelector(harriers::isMovementHistory)));
	ASSERT_EQ(harriers::damagePercent(*branch, strike), 20);
	const auto moved = cache.getDamage(source.get(), target.get(), branch); EXPECT_GT(moved, baseline);
	EXPECT_EQ(moved, branch->battleExpectedLuckDamage(strike));
	EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
	branch->nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_FALSE(target->hasBonus(CSelector(harriers::isMovementHistory)));
	ASSERT_EQ(harriers::damagePercent(*branch, strike), 0);
	EXPECT_EQ(cache.getDamage(source.get(), target.get(), branch), baseline);
}
TEST_F(NewHorizonsStingHarriersTest, DrakonFutureOrdinaryApproachUsesExistingMovementOwnerWithoutOtherReactionPolicy)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5)); beginCombat(); HarriersEnvironment env(gameState());
	auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known); DamageCache cache; BattleExchangeVariant exchange;
	const auto before = branch->battleGetUnitByID(victim->unitId())->getPosition();
	exchange.trackAttack(branch->getForUpdate(victim->unitId()), branch->getForUpdate(actor->unitId()), false,
		false, cache, branch, false, true);
	EXPECT_NE(branch->battleGetUnitByID(victim->unitId())->getPosition(), before);
	EXPECT_TRUE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_FALSE(victim->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, HiddenDrakonPolicyDoesNotAuthorizeNewHistoryOrDamageProjection)
{
	prepare(true); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1)));
	EXPECT_FALSE(harriers::trackingRequired(*branch));
	branch->projectVoluntaryMovement(victim->unitId(), BattleHex(5, 5));
	EXPECT_FALSE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	EXPECT_EQ(harriers::damagePercent(*branch, BattleAttackInfo(branch->battleGetUnitByID(actor->unitId()),
		branch->battleGetUnitByID(victim->unitId()), 0, false)), 0);
}
TEST_F(NewHorizonsStingHarriersTest, BerserkCandidateOwnersNeverSupplyTheOrdinaryVoluntaryApproachReceipt)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::OTHER, 1, BonusSourceID()));
	HarriersEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known); DamageCache cache;
	PotentialTargets targets(branch->battleGetUnitByID(victim->unitId()), cache, branch); ASSERT_TRUE(targets.berserk);
	ASSERT_FALSE(targets.possibleAttacks.empty());
	for(const auto & candidate : targets.possibleAttacks)
	{
		EXPECT_FALSE(candidate.validatedVoluntaryApproach);
		if(candidate.effectPreview)
			EXPECT_FALSE(candidate.effectPreview->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	}
}
TEST_P(HistoricalStingHarriersTest, HistoricalAndZeroDrakonPoliciesDoNotAcquireMovementHistoryInOrdinaryPreviews)
{
	captureMode = GetParam(); prepare(true, false, true); victim->setPosition(BattleHex(6, 5));
	HarriersEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known); DamageCache cache;
	EXPECT_FALSE(harriers::trackingRequired(*branch));
	PotentialTargets targets(branch->battleGetUnitByID(victim->unitId()), cache, branch);
	for(const auto & candidate : targets.possibleAttacks)
		if(candidate.effectPreview)
			EXPECT_FALSE(candidate.effectPreview->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
}
#endif
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsStingHarriersTest, ArrivedPrefixReceiptRetainsOriginalOriginForSelectedReplay)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5)); beginCombat(); HarriersEnvironment env(gameState());
	auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto original = std::make_shared<HypotheticBattle>(&env, known);
	auto arrived = std::make_shared<HypotheticBattle>(&env, original);
	arrived->projectVoluntaryMovement(victim->unitId(), BattleHex(4, 5));
	const auto * mover = arrived->battleGetUnitByID(victim->unitId());
	ASSERT_EQ(mover->getPosition(), BattleHex(4, 5)); DamageCache cache;
	const auto receipt = AttackPossibility::evaluate(BattleAttackInfo(mover, arrived->battleGetUnitByID(actor->unitId()), 2, false),
		BattleHex(4, 5), cache, arrived, false, std::nullopt, nullptr, true, BattleHex(6, 5));
	EXPECT_EQ(receipt.validatedApproachOrigin, BattleHex(6, 5));
	EXPECT_TRUE(gnollHarriersAI::replaysSameApproach(receipt, *original->battleGetUnitByID(victim->unitId())));
	// Characterizes the arrived recursion boundary; it does not simulate an actual Overwatch shooter.
	auto selected = std::make_shared<HypotheticBattle>(&env, original); BattleExchangeVariant exchange;
	exchange.trackAttack(receipt, selected, cache);
	EXPECT_TRUE(selected->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
}
TEST_F(NewHorizonsStingHarriersTest, StaleOrAbsentMovementReceiptNeverErasesAnExistingCanonicalHistoryFact)
{
	prepare(true, false, true); victim->setPosition(BattleHex(6, 5)); HarriersEnvironment env(gameState());
	auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known); DamageCache cache;
	auto world = std::make_shared<VisibleCandidateWorld>(branch);
	const auto receipt = VisibleCandidateWorld::evaluate(world, branch->battleGetUnitByID(victim->unitId()),
		branch->battleGetUnitByID(actor->unitId()), 2, false, BattleHex(4, 5), cache, 0, true);
	ASSERT_TRUE(gnollHarriersAI::replaysSameApproach(receipt, *branch->battleGetUnitByID(victim->unitId())));
	branch->moveUnit(victim->unitId(), BattleHex(5, 5));
	EXPECT_FALSE(gnollHarriersAI::replaysSameApproach(receipt, *branch->battleGetUnitByID(victim->unitId())));
	EXPECT_FALSE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	gnollHarriersAI::restoreEffects(*branch, receipt, false);
	EXPECT_FALSE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	branch->projectVoluntaryMovement(victim->unitId(), BattleHex(4, 5));
	ASSERT_TRUE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
	gnollHarriersAI::restoreEffects(*branch, receipt, false);
	EXPECT_TRUE(branch->battleGetUnitByID(victim->unitId())->hasBonus(CSelector(harriers::isMovementHistory)));
}
#endif
#if ENABLE_BATTLE_AI
namespace
{
float stingBudgetUtility(const std::shared_ptr<CBattleInfoCallback> & battle, const battle::Unit & source,
	const battle::Unit & target, int64_t base, int remaining, DamageCache & cache)
{
	auto query = target.acquireState(); query->physicalPoisonBaseDamage = base;
	query->physicalPoisonActivationsRemaining = remaining;
	return AttackPossibility::calculateDamageReduce(&source, &target,
		physicalPoisonValue::projectedDamage(query.get()), cache, battle);
}
}
TEST_F(NewHorizonsStingHarriersTest, SyncaNominalPoisonValueCoalescesRepeatedStrongestOutcomesWithoutInstallingPoison)
{
	chance = 20; prepare(); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	DamageCache cache; toxicStingAI::Opportunities opportunities;
	const auto sourceBefore = source->save(), targetBefore = target->save();
	const auto utility = stingBudgetUtility(branch, *source, *target, 10, 3, cache); ASSERT_GT(utility, 0);
	BattleAttackInfo hit(source.get(), target.get(), 0, false);
	EXPECT_NEAR(opportunities.value(branch, hit, 40, cache), .2f * utility, 1e-4f);
	EXPECT_NEAR(opportunities.value(branch, hit, 40, cache), .16f * utility, 1e-4f);
	EXPECT_EQ(source->save(), sourceBefore); EXPECT_EQ(target->save(), targetBefore);
	EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaCorrelatesScorpicorePrefixAndRerollAcrossRepeatedPoisonOpportunities)
{
	chance = 20; prepare(false, true); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	DamageCache cache; toxicStingAI::Opportunities opportunities; BattleAttackInfo hit(source.get(), target.get(), 0, false);
	const auto utility = stingBudgetUtility(branch, *source, *target, 10, 3, cache);
	EXPECT_NEAR(opportunities.value(branch, hit, 40, cache), .072f * utility, 1e-4f);
	EXPECT_NEAR(opportunities.value(branch, hit, 40, cache), .10368f * utility, 1e-4f);
	EXPECT_EQ(branch->getAdverseCombatRerollState(BattleSide::DEFENDER), adverse);
	EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaProjectedSpentResourceOverridesNativeFreeWithoutMutatingEither)
{
	chance = 20; prepare(); AdverseCombatRerollState free; free.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, free); HarriersEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto branch = std::make_shared<HypotheticBattle>(&env, parent); auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	auto spent = free; spent.used = true; branch->setAdverseCombatRerollState(BattleSide::DEFENDER, spent);
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	DamageCache cache; toxicStingAI::Opportunities opportunities;
	const auto utility = stingBudgetUtility(branch, *source, *target, 10, 3, cache);
	EXPECT_NEAR(opportunities.value(branch, BattleAttackInfo(source.get(), target.get(), 0, false), 40, cache), .2f * utility, 1e-4f);
	EXPECT_EQ(branch->getAdverseCombatRerollState(BattleSide::DEFENDER), spent);
	EXPECT_EQ(parent->getAdverseCombatRerollState(BattleSide::DEFENDER), free);
	EXPECT_EQ(sibling->getAdverseCombatRerollState(BattleSide::DEFENDER), free);
	EXPECT_EQ(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER), free);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaRefreshUsesUtilityDifferenceAndWeakerPoisonSkipsOnlyItsOwnRoll)
{
	chance = 20; prepare(false, true); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	target->physicalPoisonBaseDamage = 10; target->physicalPoisonActivationsRemaining = 1;
	DamageCache cache; toxicStingAI::Opportunities opportunities; BattleAttackInfo hit(source.get(), target.get(), 0, false);
	EXPECT_FLOAT_EQ(opportunities.value(branch, hit, 4, cache), 0);
	const auto increment = stingBudgetUtility(branch, *source, *target, 10, 3, cache)
		- stingBudgetUtility(branch, *source, *target, 10, 1, cache);
	// First weaker attempt still allowed Scorpicore's preceding harmful roll.
	EXPECT_NEAR(opportunities.value(branch, hit, 40, cache), .0976f * increment, 1e-4f);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 1);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaValuesNonlinearPoisonBudgetsBeforeWeightAndGivesNoCreditPastLethalSaturation)
{
	chance = 20; prepare(); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	int64_t injury = target->getAvailableHealth() - 30; target->damage(injury);
	DamageCache cache; toxicStingAI::Opportunities opportunities; BattleAttackInfo hit(source.get(), target.get(), 0, false);
	const auto full = stingBudgetUtility(branch, *source, *target, 10, 3, cache);
	EXPECT_NEAR(opportunities.value(branch, hit, 40, cache), .2f * full, 1e-4f);
	EXPECT_GT(full, 0);
	target->physicalPoisonBaseDamage = 10; target->physicalPoisonActivationsRemaining = 3;
	toxicStingAI::Opportunities saturated;
	EXPECT_FLOAT_EQ(saturated.value(branch, hit, 44, cache), 0);
	EXPECT_EQ(target->physicalPoisonBaseDamage, 10);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaFractionalNominalRerollUsesWeightedRoundedUtilitiesAndHiddenBiasRemainsUnknown)
{
	chance = 20; prepare(); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	source->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
		BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS, BonusSource::OTHER, 4125, BonusSourceID())});
	DamageCache cache; toxicStingAI::Opportunities opportunities;
	const auto utility = stingBudgetUtility(branch, *source, *target, 10, 3, cache);
	EXPECT_NEAR(opportunities.value(branch, BattleAttackInfo(source.get(), target.get(), 0, false), 40, cache), .006825f * utility, 1e-4f);
	EXPECT_EQ(branch->getAdverseCombatRerollState(BattleSide::DEFENDER), adverse);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaRealCandidateCachedSelectedReplayCarriesValueOnceAndPreservesBothRngStreams)
{
	chance = 20; prepare(); beginCombat(); HarriersEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent); auto selected = std::make_shared<HypotheticBattle>(&env, parent);
	const auto sourceBefore = actor->save(), targetBefore = victim->save();
	auto * nativeRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRng = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(nativeRng, nullptr); ASSERT_NE(fixtureRng, nullptr);
	const auto nativeBefore = harriersAIWire(*nativeRng), fixtureBefore = harriersAIWire(*fixtureRng);
	DamageCache cache; PotentialTargets targets(parent->battleGetUnitByID(actor->unitId()), cache, parent);
	const auto chosen = std::ranges::find_if(targets.possibleAttacks, [this](const auto & ap)
		{ return ap.attack.defender->unitId() == victim->unitId() && ap.from == actor->getPosition(); });
	ASSERT_NE(chosen, targets.possibleAttacks.end()); ASSERT_FALSE(chosen->fortuneStrikes.empty());
	const auto & hit = chosen->fortuneStrikes.front().resolvedHits; ASSERT_FALSE(hit.empty());
	const auto physical = AttackPossibility::calculateDamageReduce(actor, victim, hit.front().second, cache, parent);
	EXPECT_GT(chosen->defenderDamageReduce, physical);
	const auto cached = *chosen; BattleExchangeVariant exchange;
	exchange.trackAttack(cached, selected, cache);
	EXPECT_NEAR(exchange.getScore().enemyDamageReduce, cached.defenderDamageReduce + cached.shootersBlockedDmg, 1e-4f);
	EXPECT_EQ(selected->battleGetUnitByID(victim->unitId())->acquireState()->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(sibling->battleGetUnitByID(victim->unitId())->acquireState()->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(actor->save(), sourceBefore); EXPECT_EQ(victim->save(), targetBefore);
	EXPECT_EQ(harriersAIWire(*nativeRng), nativeBefore); EXPECT_EQ(harriersAIWire(*fixtureRng), fixtureBefore);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaFutureAdjacentAndMovingActionsUseFullExistingPreviewWithoutNewMovementSearch)
{
	chance = 20; prepare(); beginCombat(); HarriersEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	for(const bool moving : {false, true})
	{
		auto branch = std::make_shared<HypotheticBattle>(&env, parent);
		if(moving) branch->moveUnit(victim->unitId(), BattleHex(6, 5));
		DamageCache cache; BattleExchangeVariant exchange; BattleExchangeAttackResult action;
		exchange.trackAttack(branch->getForUpdate(actor->unitId()), branch->getForUpdate(victim->unitId()), false,
			true, cache, branch, false, true, nullptr, nullptr, nullptr, &action);
		EXPECT_TRUE(action.completedAction);
		EXPECT_GT(exchange.getScore().enemyDamageReduce, 0);
		EXPECT_EQ(branch->battleGetUnitByID(victim->unitId())->acquireState()->physicalPoisonBaseDamage, 0);
	}
}
TEST_P(HistoricalStingHarriersTest, SyncaHistoricalAndExplicitZeroHaveNoNominalPoisonUtilityOrStatefulAdmission)
{
	captureMode = GetParam(); prepare(); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	DamageCache cache; toxicStingAI::Opportunities opportunities;
	EXPECT_EQ(sting::chancePercent(*branch, BattleAttackInfo(actor, victim, 0, false)), 0);
	EXPECT_FLOAT_EQ(opportunities.value(branch, BattleAttackInfo(actor, victim, 0, false), 40, cache), 0);
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*branch));
}
TEST_F(NewHorizonsStingHarriersTest, SyncaHiddenPolicyAndNonordinaryOrZeroBodyFactsDoNotForgePoison)
{
	chance = 20; prepare(); HarriersEnvironment env(gameState());
	auto hidden = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1)));
	DamageCache cache; toxicStingAI::Opportunities opportunities;
	EXPECT_FLOAT_EQ(opportunities.value(hidden, BattleAttackInfo(actor, victim, 0, false), 40, cache), 0);
	auto known = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	for(int excluded = 0; excluded < 6; ++excluded)
	{
		BattleAttackInfo hit(actor, victim, 0, false);
		if(excluded == 0) hit.secondaryAttack = true;
		if(excluded == 1) hit.shooting = true;
		if(excluded == 2) hit.physicalDamage = false;
		if(excluded == 3) hit.bracePreemptive = true;
		if(excluded == 4) hit.cleaveDamagePercent = 50;
		EXPECT_FLOAT_EQ(opportunities.value(known, hit, excluded == 5 ? 0 : 40, cache), 0);
	}
	EXPECT_EQ(victim->physicalPoisonBaseDamage, 0);
}
#endif
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsStingHarriersTest, SyncaOrdinaryRetaliationAddsNominalPoisonUtilityToTheOpponentCost)
{
	chance = 20; prepare(); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(branch->battleGetUnitByID(victim->unitId()),
		branch->battleGetUnitByID(actor->unitId()), 0, false), victim->getPosition(), cache, branch);
	const auto counter = std::ranges::find_if(preview.fortuneStrikes, [this](const auto & strike)
		{ return strike.retaliation && strike.attackerId == actor->unitId(); });
	ASSERT_NE(counter, preview.fortuneStrikes.end()); ASSERT_FALSE(counter->resolvedHits.empty());
	const auto injury = AttackPossibility::calculateDamageReduce(actor, victim, counter->resolvedHits.front().second, cache, branch);
	EXPECT_GT(preview.attackerDamageReduce, injury);
	EXPECT_EQ(preview.attackerState->physicalPoisonBaseDamage, 0);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaRealShieldOnlyPrimaryCallerSuppliesZeroBodyLossAndNoPoisonValue)
{
	chance = 20; prepare(); victim->health.addTemporaryHitPoints(100000); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(branch->battleGetUnitByID(actor->unitId()),
		branch->battleGetUnitByID(victim->unitId()), 0, false), actor->getPosition(), cache, branch);
	ASSERT_FALSE(preview.fortuneStrikes.empty()); ASSERT_FALSE(preview.fortuneStrikes.front().resolvedHits.empty());
	const auto injury = preview.fortuneStrikes.front().resolvedHits.front().second;
	EXPECT_GT(injury, 0);
	const auto physical = AttackPossibility::calculateDamageReduce(actor, victim, injury, cache, branch);
	EXPECT_NEAR(preview.defenderDamageReduce, physical, 1e-4f);
	EXPECT_EQ(victim->physicalPoisonBaseDamage, 0);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaVisibleOwnPolicyWithHiddenEnemyRerollStillReceivesNominalMarginalValue)
{
	chance = 20; prepare(); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse); HarriersEnvironment env(gameState());
	auto visible = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto source = visible->getForUpdate(actor->unitId()), target = visible->getForUpdate(victim->unitId());
	ASSERT_FALSE(attackAbilityPrefix::query(*visible, *source, *target, false).adverseFactVisible);
	DamageCache cache; toxicStingAI::Opportunities opportunities;
	const auto utility = stingBudgetUtility(visible, *source, *target, 10, 3, cache);
	EXPECT_NEAR(opportunities.value(visible, BattleAttackInfo(source.get(), target.get(), 0, false), 40, cache), .2f * utility, 1e-4f);
	EXPECT_EQ(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER), adverse);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaCertainNominalApplicationRefreshesWithoutSpendingVisibleAdverseAllowance)
{
	prepare(); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	DamageCache cache; toxicStingAI::Opportunities opportunities; BattleAttackInfo hit(source.get(), target.get(), 0, false);
	const auto utility = stingBudgetUtility(branch, *source, *target, 10, 3, cache);
	EXPECT_NEAR(opportunities.value(branch, hit, 40, cache), utility, 1e-4f);
	EXPECT_FLOAT_EQ(opportunities.value(branch, hit, 40, cache), 0);
	EXPECT_EQ(branch->getAdverseCombatRerollState(BattleSide::DEFENDER), adverse);
	EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
}
#endif
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsStingHarriersTest, SyncaChanceQueryUsesNativeOwnAttackExpiryWithoutSpendingTheBorrowedSource)
{
	chance = 20; prepare(); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	source->addUnitBonus({Bonus(BonusDuration::UNTIL_OWN_ATTACK,
		BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS, BonusSource::OTHER, 5000, BonusSourceID())});
	const auto before = source->getBonusesOfType(BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS)->toJsonNode();
	DamageCache cache; toxicStingAI::Opportunities own, counter;
	const auto utility = stingBudgetUtility(branch, *source, *target, 10, 3, cache);
	BattleAttackInfo hit(source.get(), target.get(), 0, false);
	EXPECT_NEAR(own.value(branch, hit, 40, cache), .2f * utility, 1e-4f);
	hit.retaliation = true;
	EXPECT_NEAR(counter.value(branch, hit, 40, cache), .1f * utility, 1e-4f);
	EXPECT_EQ(source->getBonusesOfType(BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS)->toJsonNode(), before);
}
#endif
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsStingHarriersTest, SyncaJointNominalResourceCorrelatesTwoRecipientsAndTheFirstRecipientsRefresh)
{
	chance = 20; prepare(); auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 5), 200);
	ASSERT_NE(other, nullptr); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), first = branch->getForUpdate(victim->unitId()), second = branch->getForUpdate(other->unitId());
	DamageCache cache; toxicStingAI::Opportunities opportunities;
	const auto firstUtility = stingBudgetUtility(branch, *source, *first, 10, 3, cache);
	const auto secondUtility = stingBudgetUtility(branch, *source, *second, 10, 3, cache);
	EXPECT_NEAR(opportunities.value(branch, BattleAttackInfo(source.get(), first.get(), 0, false), 40, cache), .04f * firstUtility, 1e-4f);
	EXPECT_NEAR(opportunities.value(branch, BattleAttackInfo(source.get(), second.get(), 0, false), 40, cache), .072f * secondUtility, 1e-4f);
	EXPECT_NEAR(opportunities.value(branch, BattleAttackInfo(source.get(), first.get(), 0, false), 40, cache), .0896f * firstUtility, 1e-4f);
	EXPECT_EQ(first->physicalPoisonBaseDamage, 0); EXPECT_EQ(second->physicalPoisonBaseDamage, 0);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaWholeCommandDoesNotValuePoisonOnAPrimaryVictimKilledByTheLaterBlow)
{
	chance = 20; prepare(); actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID())); HarriersEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	DamageCache cache;
	const auto damage = cache.getDamage(parent->battleGetUnitByID(actor->unitId()), parent->battleGetUnitByID(victim->unitId()), parent);
	auto target = parent->getForUpdate(victim->unitId()); int64_t injury = target->getAvailableHealth() - damage - 1; ASSERT_GT(injury, 0);
	target->damage(injury);
	auto control = std::make_shared<HypotheticBattle>(&env, parent);
	control->addUnitBonus(victim->unitId(), {Bonus(BonusDuration::ONE_BATTLE, BonusType::NON_LIVING, BonusSource::OTHER, 1, BonusSourceID())});
	const auto valued = AttackPossibility::evaluate(BattleAttackInfo(parent->battleGetUnitByID(actor->unitId()), target.get(), 0, false), actor->getPosition(), cache, parent);
	const auto immune = AttackPossibility::evaluate(BattleAttackInfo(control->battleGetUnitByID(actor->unitId()), control->battleGetUnitByID(victim->unitId()), 0, false), actor->getPosition(), cache, control);
	ASSERT_TRUE(valued.defenderDead); ASSERT_TRUE(immune.defenderDead);
	EXPECT_NEAR(valued.defenderDamageReduce, immune.defenderDamageReduce, 1e-4f);
}
TEST_F(NewHorizonsStingHarriersTest, SyncaFinalOpportunityCompletionPricesTheSurvivorOnceWithoutStateMutation)
{
	chance = 20; prepare(); HarriersEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	BattleAttackInfo hit(source.get(), target.get(), 0, false); AttackPossibility preview(source->getPosition(), target->getPosition(), hit);
	preview.attackerState = source; preview.affectedUnits.push_back(target); DamageCache cache; toxicStingAI::Opportunities opportunities;
	const auto plan = sting::prepare(*branch, hit, 40); ASSERT_TRUE(plan);
	opportunities.score(preview, branch, hit, plan, cache); EXPECT_FLOAT_EQ(preview.defenderDamageReduce, 0);
	const auto utility = stingBudgetUtility(branch, *source, *target, 10, 3, cache);
	opportunities.complete(preview, branch, cache); EXPECT_NEAR(preview.defenderDamageReduce, .2f * utility, 1e-4f);
	const auto once = preview.defenderDamageReduce; opportunities.complete(preview, branch, cache);
	EXPECT_FLOAT_EQ(preview.defenderDamageReduce, once); EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
}
#endif
#if ENABLE_BATTLE_AI
namespace
{
JsonNode stingExportSnapshot(const std::shared_ptr<InitialExportBattleSubject> & subject)
{
	JsonNode result;
	for(size_t i = 0; i < subject->world().size(); ++i)
	{
		JsonNode unit;
		unit["state"] = subject->world().snapshotAt(i);
		unit["effects"] = subject->world().unitAt(i).getUnstackedBonuses(Selector::all)->toJsonNode();
		result.Vector().push_back(std::move(unit));
	}
	return result;
}
JsonNode stingOwnedSnapshot(const std::shared_ptr<HypotheticBattle> & branch)
{
	const auto subject = branch->initialExportSubject();
	JsonNode result;
	for(size_t i = 0; i < subject->world().size(); ++i)
	{
		const auto unit = branch->getForUpdate(subject->world().sourceAt(i).unitId);
		JsonNode entry;
		entry["state"] = unit->save();
		entry["effects"] = unit->getUnstackedBonuses(Selector::all)->toJsonNode();
		result["units"].Vector().push_back(std::move(entry));
	}
	result["export"] = stingExportSnapshot(subject);
	return result;
}
}
TEST_F(NewHorizonsStingHarriersTest, SyncaOwnedExportQueryExpiresOnlyItsPrivateForkAndCopiesPosthitFormScalars)
{
	chance = 20; prepare(false, true);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
		BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS, BonusSource::OTHER, 5000, BonusSourceID()));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_BEING_ATTACKED, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	AdverseCombatRerollState free; free.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, free); HarriersEnvironment env(gameState());
	std::shared_ptr<CBattleInfoCallback> fullInformation(gameState(), battle());
	const auto nativeSource = actor->save(), nativeTarget = victim->save();
	auto * nativeRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRng = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(nativeRng, nullptr); ASSERT_NE(fixtureRng, nullptr);
	const auto nativeRngBefore = harriersAIWire(*nativeRng), fixtureRngBefore = harriersAIWire(*fixtureRng);
	struct Control { float value = 0, utility = 0; JsonNode target, posthit; };
	std::vector<Control> controls;
	// Legacy ordinary form queries use temporary native attachments. Finish ALL
	// such controls before capturing an immutable originating export graph.
	for(const bool spent : {false, true})
	{
		auto ordinary = std::make_shared<HypotheticBattle>(&env, fullInformation);
		auto resource = free; resource.used = spent;
		ordinary->setAdverseCombatRerollState(BattleSide::DEFENDER, resource);
		auto target = ordinary->getForUpdate(victim->unitId());
		target->beginBattleForm(creatureByName("core:halberdier"), 2);
		int64_t injury = 13; target->damage(injury);
		auto posthit = target->acquireState(); injury = 7; posthit->damage(injury);
		const auto ordinarySource = ordinary->getForUpdate(actor->unitId());
		EXPECT_TRUE(attackAbilityPrefix::query(*ordinary, *ordinarySource, *posthit, false).eligibleHarmfulAttempts.empty());
		EXPECT_EQ(creatureAbilityChance::rounding(*ordinarySource, 20).wholePercent, 10);
		const auto ordinarySourceBefore = ordinarySource->save();
		const auto ordinarySourceEffects = ordinarySource->getUnstackedBonuses(Selector::all)->toJsonNode();
		const auto ordinaryTargetBefore = target->save(), ordinaryPosthitBefore = posthit->save();
		const auto ordinaryTargetEffects = target->getUnstackedBonuses(Selector::all)->toJsonNode();
		DamageCache controlCache, expectationCache; toxicStingAI::Opportunities ordinaryQuery;
		Control control;
		control.value = ordinaryQuery.value(ordinary, BattleAttackInfo(ordinary->getForUpdate(actor->unitId()).get(), posthit.get(), 0, false), 40, controlCache);
		control.utility = stingBudgetUtility(ordinary, *ordinary->getForUpdate(actor->unitId()), *posthit, 10, 3, expectationCache);
		control.target = target->save(); control.posthit = posthit->save();
		ASSERT_GT(control.utility, 0);
		// The private query must expose the expired immunity to canonical spell
		// target resolution, without expiring it on the borrowed parent recipient.
		EXPECT_NEAR(control.value, (spent ? .2f : .072f) * control.utility, 1e-4f);
		EXPECT_EQ(target->save(), ordinaryTargetBefore); EXPECT_EQ(posthit->save(), ordinaryPosthitBefore);
		EXPECT_EQ(ordinarySource->save(), ordinarySourceBefore);
		EXPECT_EQ(ordinarySource->getUnstackedBonuses(Selector::all)->toJsonNode(), ordinarySourceEffects);
		EXPECT_TRUE(attackAbilityPrefix::query(*ordinary, *ordinarySource, *posthit, false).eligibleHarmfulAttempts.empty());
		EXPECT_EQ(target->getUnstackedBonuses(Selector::all)->toJsonNode(), ordinaryTargetEffects);
		EXPECT_EQ(ordinary->getAdverseCombatRerollState(BattleSide::DEFENDER), resource);
		controls.push_back(std::move(control));
	}
	EXPECT_EQ(actor->save(), nativeSource); EXPECT_EQ(victim->save(), nativeTarget);
	EXPECT_EQ(harriersAIWire(*nativeRng), nativeRngBefore); EXPECT_EQ(harriersAIWire(*fixtureRng), fixtureRngBefore);
	// Explicit authorized all-knowing fixture source; no later legacy form query
	// may mutate the originating graph's generation during this borrow interval.
	auto origin = InitialExportBattleSubject::capture(fullInformation, *battle());
	const auto originBefore = stingExportSnapshot(origin);
	for(const bool spent : {false, true})
	{
		auto parent = std::make_shared<HypotheticBattle>(&env, origin);
		auto expectedResource = free; expectedResource.used = spent;
		parent->setAdverseCombatRerollState(BattleSide::DEFENDER, expectedResource);
		auto formedTarget = parent->getForUpdate(victim->unitId());
		formedTarget->beginBattleForm(creatureByName("core:halberdier"), 2);
		int64_t initialInjury = 13; formedTarget->damage(initialInjury);
		auto source = parent->getForUpdate(actor->unitId()), target = parent->getForUpdate(victim->unitId());
		ASSERT_TRUE(target->hasBattleForm()); EXPECT_EQ(target->getBattleFormRoundsRemaining(), 2);
		auto posthit = target->acquireState(); int64_t injury = 7; posthit->damage(injury);
		const auto posthitBefore = posthit->save();
		// Materialize both units before querying the parent.
		auto materialized = std::make_shared<HypotheticBattle>(&env, parent);
		auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
		materialized->getForUpdate(actor->unitId()); materialized->getForUpdate(victim->unitId());
		sibling->getForUpdate(actor->unitId()); sibling->getForUpdate(victim->unitId());
		const auto parentBefore = stingOwnedSnapshot(parent), childBefore = stingOwnedSnapshot(materialized), siblingBefore = stingOwnedSnapshot(sibling);
		EXPECT_EQ(creatureAbilityChance::rounding(*source, 20).wholePercent, 10);
		EXPECT_TRUE(attackAbilityPrefix::query(*parent, *source, *target, false).eligibleHarmfulAttempts.empty());
		DamageCache cache; toxicStingAI::Opportunities ownedQuery;
		const auto value = ownedQuery.value(parent, BattleAttackInfo(source.get(), posthit.get(), 0, false), 40, cache);
		const auto & expected = controls.at(spent ? 1 : 0);
		const auto control = expected.value, utility = expected.utility;
		EXPECT_EQ(target->save(), expected.target);
		EXPECT_EQ(posthit->save(), expected.posthit);
		ASSERT_GT(utility, 0);
		EXPECT_NEAR(value, (spent ? .2f : .072f) * utility, 1e-4f);
		EXPECT_NEAR(value, control, 1e-4f);
		EXPECT_EQ(stingOwnedSnapshot(parent), parentBefore);
		EXPECT_EQ(stingOwnedSnapshot(materialized), childBefore);
		EXPECT_EQ(stingOwnedSnapshot(sibling), siblingBefore);
		EXPECT_EQ(posthit->save(), posthitBefore);
		EXPECT_EQ(parent->getAdverseCombatRerollState(BattleSide::DEFENDER), expectedResource);
		EXPECT_EQ(materialized->getAdverseCombatRerollState(BattleSide::DEFENDER), expectedResource);
		EXPECT_EQ(sibling->getAdverseCombatRerollState(BattleSide::DEFENDER), expectedResource);
		EXPECT_EQ(stingExportSnapshot(origin), originBefore);
		EXPECT_EQ(actor->save(), nativeSource); EXPECT_EQ(victim->save(), nativeTarget);
		EXPECT_EQ(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER), free);
		EXPECT_EQ(harriersAIWire(*nativeRng), nativeRngBefore); EXPECT_EQ(harriersAIWire(*fixtureRng), fixtureRngBefore);
	}
}
#endif

TEST_F(NewHorizonsStingHarriersTest, NativeApplicationObserverCountsIdenticalProducerRefreshesButNotUnchangedSnapshots)
{
	prepare(); beginCombat();
	const auto plan = sting::prepare(*battle(), BattleAttackInfo(actor, victim, 0, false), 20);
	ASSERT_TRUE(plan); ASSERT_EQ(plan->chancePercent, 100);
	battleToxicSting::apply(*gameHandler, *gameHandler->battles, *battle(), plan);
	ASSERT_EQ(observed.poisonApplications, 1);
	const auto first = victim->save();
	BattleUnitsChanged snapshot; snapshot.battleID = BattleID(0);
	UnitChanges unchanged(victim->unitId(), UnitChanges::EOperation::UPDATE); unchanged.data = first;
	snapshot.changedStacks.push_back(std::move(unchanged)); gameHandler->sendAndApply(snapshot);
	EXPECT_EQ(observed.poisonApplications, 1); EXPECT_EQ(victim->save(), first);
	battleToxicSting::apply(*gameHandler, *gameHandler->battles, *battle(), plan);
	EXPECT_EQ(observed.poisonApplications, 2); EXPECT_EQ(victim->save(), first);
	EXPECT_EQ(observed.poisonTarget, victim->unitId()); EXPECT_EQ(observed.poisonSource, actor->unitId());
	EXPECT_EQ(observed.poisonBase, plan->baseDamage); EXPECT_EQ(victim->physicalPoisonActivationsRemaining, 3);
	ASSERT_EQ(observed.ordered.size(), 2u); EXPECT_EQ(observed.ordered[0], "poison"); EXPECT_EQ(observed.ordered[1], "poison");
}
