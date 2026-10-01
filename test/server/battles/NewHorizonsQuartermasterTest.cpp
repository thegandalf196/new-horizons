/*
 * NewHorizonsQuartermasterTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/BattleAI.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleUnitTurnReason.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../../lib/battle/ReducedExtraActivationState.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr auto warMachinesSkillId = "new-horizons:warMachines";
constexpr auto quartermasterPerkId = "new-horizons:warMachines.quartermaster";
constexpr auto masterGunnerPerkId = "new-horizons:warMachines.masterGunner";

bool setPerkActive(JsonNode & rules, const std::string_view perkId)
{
	auto & perks = rules["skills"][warMachinesSkillId]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class QuartermasterEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit QuartermasterEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsQuartermasterTest : public BattleTestFixture
{
protected:
	CGTownInstance * fortifiedTown = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setPerkActive(perkRules, quartermasterPerkId)
			|| !setPerkActive(perkRules, masterGunnerPerkId))
			throw std::runtime_error("Missing Quartermaster or Master Gunner from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, std::move(capabilities));
	}

	SecondarySkill warMachines() const
	{
		const int decoded = SecondarySkill::decode(warMachinesSkillId);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, const std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [perkId](const auto & candidate)
			{
				return candidate.selection.skillId == warMachinesSkillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(warMachinesSkillId, std::string(perkId)));
			return;
		}

		FAIL() << "No legal New Horizons perk offer contained " << perkId;
	}

	void prepareWarMachines(CGHeroInstance * hero, const bool quartermaster)
	{
		const auto skill = warMachines();
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(warMachinesSkillId,
			hero->getSecSkillLevel(skill)));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(warMachinesSkillId), MasteryLevel::BASIC);

		if(quartermaster)
			acceptPerkThroughOffer(hero, quartermasterPerkId);
		else
			ASSERT_FALSE(hero->hasActivePerk(warMachinesSkillId, quartermasterPerkId));
	}

	void prepareFieldBattle(const bool quartermaster,
		const bool includeBallista = true, const bool includeAmmoCart = true)
	{
		startGame();
		prepareWarMachines(attackerSideHero, quartermaster);
		if(includeBallista)
			giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
		if(includeAmmoCart)
			giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
		startBattle();
	}

	void prepareFortifiedTownBattle()
	{
		startGame(true);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		fortifiedTown = towns.front();
		ASSERT_EQ(fortifiedTown->fortLevel(), CGTownInstance::FORT);

		for(const auto resource : {GameResID(GameResID::WOOD), GameResID(GameResID::ORE), GameResID(GameResID::GOLD)})
			grantResources(PlayerColor(1), resource, 100000);
		ASSERT_TRUE(gameHandler->buildStructure(fortifiedTown->id, BuildingID::CITADEL));
		ASSERT_GT(fortifiedTown->fortificationsLevel().wallsHealth, 0);

		prepareWarMachines(attackerSideHero, true);
		giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
		giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
		startBattle(fortifiedTown);
	}

	void prepareFirstAidTentBattle()
	{
		startGame();
		attackerSideHero->setSecSkillLevel(SecondarySkill::FIRST_AID,
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		prepareWarMachines(attackerSideHero, true);
		giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
		giveArtifact(attackerSideHero, ArtifactID::FIRST_AID_TENT, ArtifactPosition::MACH3);
		startBattle();
	}

	const CStack * machine(const BattleSide side, const ArtifactID artifact) const
	{
		const auto stacks = battle()->battleGetStacksIf([side, artifact](const CStack * stack)
		{
			return stack->unitSide() == side && stack->unitType()->warMachine == artifact;
		});
		return stacks.size() == 1 ? stacks.front() : nullptr;
	}

	void advanceUntil(const CStack * target, const bool start = true)
	{
		if(start)
			beginCombat();
		const auto firstRound = battle()->getRound();
		const auto maximumTurns = battle()->stacks.size() * 4;
		for(size_t turn = 0; turn < maximumTurns && battle()->battleActiveUnit() != target; ++turn)
		{
			ASSERT_EQ(battle()->getRound(), firstRound);
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_NE(active->unitId(), target->unitId());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
		}
		ASSERT_EQ(battle()->battleActiveUnit(), target);
	}

	void injure(CStack * stack, const int64_t damage)
	{
		ASSERT_NE(stack, nullptr);
		ASSERT_GT(damage, 0);
		StacksInjured injury;
		injury.battleID = BattleID(0);
		auto & attacked = injury.stacks.emplace_back();
		attacked.attackerID = stack->unitId();
		attacked.stackAttacked = stack->unitId();
		attacked.damageAmount = damage;
		stack->prepareAttacked(attacked, gameHandler->getRandomGenerator());
		gameHandler->sendAndApply(injury);
	}

	static int64_t recordedDamage(const BattleAttack & attack)
	{
		int64_t total = 0;
		for(const auto & hit : attack.bsa)
			total += hit.damageAmount;
		return total;
	}

	std::vector<const BattleAttack *> shotsBy(const CStack * stack) const
	{
		std::vector<const BattleAttack *> result;
		for(const auto & attack : server.attacks)
			if(attack.stackAttacking == stack->unitId() && attack.shot())
				result.push_back(&attack);
		return result;
	}
};
}

TEST_F(NewHorizonsQuartermasterTest, AcceptedPerkGrantsSavedHalfOutputBallistaActivationOncePerCombat)
{
	prepareFieldBattle(true);
	const auto * ballista = machine(BattleSide::ATTACKER, ArtifactID::BALLISTA);
	const auto * ammoCart = machine(BattleSide::ATTACKER, ArtifactID::AMMO_CART);
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(ammoCart, nullptr);
	ASSERT_TRUE(ammoCart->alive());
	ASSERT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, quartermasterPerkId));
	ASSERT_FALSE(attackerSideHero->hasActivePerk(warMachinesSkillId, masterGunnerPerkId));

	auto * firstTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10000);
	auto * secondTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 2), 10000);
	ASSERT_NE(firstTarget, nullptr);
	ASSERT_NE(secondTarget, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, firstTarget->getPosition()));
	ASSERT_TRUE(battle()->battleCanShoot(ballista, secondTarget->getPosition()));

	const auto initial = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	ASSERT_TRUE(initial.enabled);
	EXPECT_FALSE(initial.used);
	EXPECT_FALSE(initial.hasActiveUnit());
	EXPECT_EQ(battle()->battleGetActivationOutputPercent(ballista), 100);

	advanceUntil(ballista);
	const auto serialBeforeActivationEnds = battle()->getActivationSerial();
	const auto normalFirst = battle()->calculateDmgRange(BattleAttackInfo(ballista, firstTarget, 0, true)).damage;
	const auto normalSecond = battle()->calculateDmgRange(BattleAttackInfo(ballista, secondTarget, 0, true)).damage;
	const auto firstBefore = firstTarget->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, firstTarget)));
	const auto firstDamage = firstBefore - firstTarget->getAvailableHealth();
	EXPECT_GE(firstDamage, normalFirst.min);
	EXPECT_LE(firstDamage, normalFirst.max);
	const auto reduced = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	ASSERT_TRUE(reduced.enabled);
	ASSERT_TRUE(reduced.used);
	ASSERT_EQ(reduced.activeUnitId, ballista->unitId());
	EXPECT_EQ(reduced.outputPercent, 50);
	EXPECT_EQ(battle()->battleActiveUnit(), ballista);
	EXPECT_EQ(battle()->battleGetActivationOutputPercent(ballista), 50);
	EXPECT_EQ(battle()->getActivationSerial(), serialBeforeActivationEnds + 1);
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(ballista));

	CMemorySerializer currentStateWire;
	currentStateWire.oser.version = ESerializationVersion::CURRENT;
	currentStateWire.iser.version = ESerializationVersion::CURRENT;
	auto savedState = reduced;
	currentStateWire.oser & savedState;
	ReducedExtraActivationState restoredState;
	currentStateWire.iser & restoredState;
	EXPECT_EQ(restoredState, reduced);

	CMemorySerializer downgradeWire;
	downgradeWire.oser.version = ESerializationVersion::NEW_HORIZONS_RANGED_FOLLOW_UP;
	auto downgradeState = reduced;
	EXPECT_THROW(downgradeWire.oser & downgradeState, std::runtime_error);
	EXPECT_TRUE(downgradeWire.extractBuffer().empty());

	QuartermasterEnvironment environment(gameState());
	const auto controller = battle()->sideToPlayer(BattleSide::ATTACKER);
	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), controller);
	HypotheticBattle projected(&environment, callback);
	const auto projectedBallista = projected.getForUpdate(ballista->unitId());
	const auto projectedFirstTarget = projected.getForUpdate(firstTarget->unitId());
	const auto projectedSecondTarget = projected.getForUpdate(secondTarget->unitId());
	ASSERT_NE(projectedBallista, nullptr);
	ASSERT_NE(projectedFirstTarget, nullptr);
	ASSERT_NE(projectedSecondTarget, nullptr);
	EXPECT_EQ(projected.battleGetReducedExtraActivationState(BattleSide::ATTACKER), reduced);
	EXPECT_EQ(projected.battleGetActivationOutputPercent(projectedBallista.get()), 50);
	const auto projectedFirstForecast = projected.calculateDmgRange(
		BattleAttackInfo(projectedBallista.get(), projectedFirstTarget.get(), 0, true)).damage;
	EXPECT_NEAR(projectedFirstForecast.min, normalFirst.min * 0.5, 1.0);
	EXPECT_NEAR(projectedFirstForecast.max, normalFirst.max * 0.5, 1.0);
	const auto projectedSerialBeforeContinuation = battle()->getActivationSerial();
	// Quartermaster and Master Gunner both occupy War Machines' Basic tier, so
	// legal progression cannot select both. Exercise their independent output
	// composition on a detached Ballista without forging the hero's perk state.
	projectedBallista->rangedFollowUpDamagePercent = 60;
	const auto projectedFollowUpForecast = projected.calculateDmgRange(
		BattleAttackInfo(projectedBallista.get(), projectedSecondTarget.get(), 0, true)).damage;
	EXPECT_NEAR(projectedFollowUpForecast.min, normalSecond.min * 0.3, 1.0);
	EXPECT_NEAR(projectedFollowUpForecast.max, normalSecond.max * 0.3, 1.0);
	EXPECT_EQ(battle()->getActivationSerial(), projectedSerialBeforeContinuation)
		<< "A projected Master Gunner continuation does not begin another activation";

	const auto reducedFirst = battle()->calculateDmgRange(BattleAttackInfo(ballista, secondTarget, 0, true)).damage;
	EXPECT_NEAR(reducedFirst.min, normalSecond.min * 0.5, 1.0);
	EXPECT_NEAR(reducedFirst.max, normalSecond.max * 0.5, 1.0);
	const auto reducedFirstBefore = secondTarget->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, secondTarget)));
	const auto reducedFirstDamage = reducedFirstBefore - secondTarget->getAvailableHealth();
	EXPECT_GE(reducedFirstDamage, reducedFirst.min);
	EXPECT_LE(reducedFirstDamage, reducedFirst.max);
	const auto finished = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	EXPECT_TRUE(finished.used);
	EXPECT_FALSE(finished.hasActiveUnit());
	EXPECT_EQ(finished.outputPercent, 100);
	EXPECT_NE(battle()->battleActiveUnit(), ballista);

	const auto shots = shotsBy(ballista);
	ASSERT_EQ(shots.size(), 2u);
	EXPECT_EQ(shots[0]->bsa.front().stackAttacked, firstTarget->unitId());
	EXPECT_EQ(shots[1]->bsa.front().stackAttacked, secondTarget->unitId());
	EXPECT_GT(recordedDamage(*shots[1]), 0);
	EXPECT_EQ(std::count_if(server.stackActivations.begin(), server.stackActivations.end(),
		[ballista](const BattleSetActiveStack & activation)
	{
		return activation.stack == ballista->unitId()
			&& activation.reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION;
	}), 1);

}

TEST_F(NewHorizonsQuartermasterTest, MissingPerkDoesNotGrantReducedExtraActivation)
{
	prepareFieldBattle(false);
	const auto * ballista = machine(BattleSide::ATTACKER, ArtifactID::BALLISTA);
	ASSERT_NE(ballista, nullptr);
	EXPECT_FALSE(battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER).enabled);
	const auto * ammoCart = machine(BattleSide::ATTACKER, ArtifactID::AMMO_CART);
	ASSERT_NE(ammoCart, nullptr);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10000);
	ASSERT_NE(target, nullptr);
	advanceUntil(ballista);
	const auto normalDamage = battle()->calculateDmgRange(BattleAttackInfo(ballista, target, 0, true)).damage;
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, target)));
	const auto damage = before - target->getAvailableHealth();
	EXPECT_GE(damage, normalDamage.min);
	EXPECT_LE(damage, normalDamage.max);
	EXPECT_FALSE(battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(std::none_of(server.stackActivations.begin(), server.stackActivations.end(),
		[ballista](const BattleSetActiveStack & activation)
	{
		return activation.stack == ballista->unitId()
			&& activation.reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION;
	}));
}

TEST_F(NewHorizonsQuartermasterTest, DestroyedAmmoCartDoesNotGrantReducedExtraActivation)
{
	prepareFieldBattle(true);
	const auto * guardedBallista = machine(BattleSide::ATTACKER, ArtifactID::BALLISTA);
	auto * guardedCart = const_cast<CStack *>(machine(BattleSide::ATTACKER, ArtifactID::AMMO_CART));
	ASSERT_NE(guardedBallista, nullptr);
	ASSERT_NE(guardedCart, nullptr);
	injure(guardedCart, guardedCart->getAvailableHealth());
	ASSERT_FALSE(guardedCart->alive());

	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10000);
	ASSERT_NE(target, nullptr);
	advanceUntil(guardedBallista);
	const auto fullDamage = battle()->calculateDmgRange(BattleAttackInfo(guardedBallista, target, 0, true)).damage;
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(guardedBallista->unitSide()), BattleAction::makeShotAttack(guardedBallista, target)));
	const auto damage = before - target->getAvailableHealth();
	EXPECT_GE(damage, fullDamage.min);
	EXPECT_LE(damage, fullDamage.max);
	const auto state = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	EXPECT_TRUE(state.enabled);
	EXPECT_FALSE(state.used);
	EXPECT_FALSE(state.hasActiveUnit());
	EXPECT_EQ(battle()->battleGetActivationOutputPercent(guardedBallista), 100);
	EXPECT_TRUE(std::none_of(server.stackActivations.begin(), server.stackActivations.end(),
		[guardedBallista](const BattleSetActiveStack & activation)
	{
		return activation.stack == guardedBallista->unitId()
			&& activation.reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION;
	}));
}

TEST_F(NewHorizonsQuartermasterTest, TentExtraActivationAppliesHalfRawHealingToWoundedTarget)
{
	prepareFirstAidTentBattle();

	const auto * tent = machine(BattleSide::ATTACKER, ArtifactID::FIRST_AID_TENT);
	ASSERT_NE(tent, nullptr);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, quartermasterPerkId));
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:azureDragon"), BattleHex(leftHex), 1);
	ASSERT_NE(target, nullptr);
	const auto fullHealth = target->getAvailableHealth();
	ASSERT_GT(fullHealth, 100);
	injure(target, fullHealth - 10);
	ASSERT_EQ(target->getAvailableHealth(), 10);

	advanceUntil(tent);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(tent->unitSide()), BattleAction::makeDefend(tent)));
	const auto reduced = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	ASSERT_TRUE(reduced.used);
	ASSERT_EQ(reduced.activeUnitId, tent->unitId());
	ASSERT_EQ(reduced.outputPercent, 50);

	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	const auto expectedRaw = static_cast<int64_t>(siege->firstAidHealing) * 50 / 100;
	EXPECT_EQ(battle()->battleGetActivationOutputPercent(tent), 50);
	EXPECT_EQ(battle()->battleGetFirstAidHealingOutput(tent), expectedRaw);
	EXPECT_EQ(battle()->getFirstAidHealValue(attackerSideHero, target), expectedRaw);

	auto environment = std::make_shared<QuartermasterEnvironment>(gameState());
	auto callback = std::make_shared<CBattleCallback>(battle()->sideToPlayer(tent->unitSide()), nullptr);
	callback->onBattleStarted(battle());
	CBattleAI ai;
	ai.initBattleInterface(environment, callback);
	const auto aiAction = ai.useHealingTent(BattleID(0), tent);
	ASSERT_EQ(aiAction.actionType, EActionType::STACK_HEAL);
	const auto aiTarget = aiAction.getTarget(battle());
	ASSERT_EQ(aiTarget.size(), 1u);
	EXPECT_EQ(aiTarget.front().unitValue, target);

	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(tent->unitSide()), aiAction));
	EXPECT_EQ(target->getAvailableHealth() - healthBefore, expectedRaw);
	const auto finished = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	EXPECT_FALSE(finished.hasActiveUnit());
	EXPECT_EQ(finished.outputPercent, 100);
}

TEST_F(NewHorizonsQuartermasterTest, TentCapsHalfRawOutputAtNearFullTargetsMissingHealth)
{
	prepareFirstAidTentBattle();

	const auto * tent = machine(BattleSide::ATTACKER, ArtifactID::FIRST_AID_TENT);
	ASSERT_NE(tent, nullptr);
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:azureDragon"), BattleHex(leftHex), 1);
	ASSERT_NE(target, nullptr);
	const auto fullHealth = target->getAvailableHealth();
	ASSERT_GT(fullHealth, 10);
	injure(target, 10);
	ASSERT_EQ(target->getAvailableHealth(), fullHealth - 10);

	advanceUntil(tent);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(tent->unitSide()), BattleAction::makeDefend(tent)));
	const auto reduced = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	ASSERT_TRUE(reduced.used);
	ASSERT_EQ(reduced.activeUnitId, tent->unitId());
	ASSERT_EQ(reduced.outputPercent, 50);

	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	const auto expectedRaw = static_cast<int64_t>(siege->firstAidHealing) * 50 / 100;
	EXPECT_GT(expectedRaw, 10);
	EXPECT_EQ(battle()->battleGetFirstAidHealingOutput(tent), expectedRaw);
	EXPECT_EQ(battle()->getFirstAidHealValue(attackerSideHero, target), 10)
		<< "Only after Quartermaster reduces raw output is it capped by missing HP";

	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(tent->unitSide()), BattleAction::makeHeal(tent, target)));
	EXPECT_EQ(target->getAvailableHealth() - healthBefore, 10);
}

TEST_F(NewHorizonsQuartermasterTest, CatapultExtraActivationHalvesActualStructuralDamageInFortifiedTown)
{
	prepareFortifiedTownBattle();
	// The test map's default armies contain one Pikeman each. The Citadel tower
	// resolves in the siege phase before the Catapult, and its automatic shot can
	// eliminate that lone attacker before the Catapult gets a player turn.
	// Keep both sides in combat through the opening tower activations so this
	// fixture reaches the accepted Catapult action below.
	auto * attackerSurvivors = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10000);
	auto * defenderSurvivors = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10000);
	ASSERT_NE(attackerSurvivors, nullptr);
	ASSERT_NE(defenderSurvivors, nullptr);

	const auto * catapult = machine(BattleSide::ATTACKER, ArtifactID::CATAPULT);
	const auto * ammoCart = machine(BattleSide::ATTACKER, ArtifactID::AMMO_CART);
	ASSERT_NE(catapult, nullptr);
	ASSERT_NE(ammoCart, nullptr);
	ASSERT_EQ(battle()->getDefendedTown(), fortifiedTown);
	ASSERT_TRUE(ammoCart->alive());
	ASSERT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, quartermasterPerkId));

	advanceUntil(catapult);
	const auto normalPerHit = battle()->battleGetCatapultStructuralDamage(catapult, 1);
	ASSERT_GT(normalPerHit, 0);
	const auto targetHexes = battle()->getAttackableWallParts();
	ASSERT_FALSE(targetHexes.empty());

	auto submitCatapultShot = [&]()
	{
		BattleAction action;
		action.actionType = EActionType::CATAPULT;
		action.side = catapult->unitSide();
		action.stackNumber = catapult->unitId();
		action.aimToHex(targetHexes.front());
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(catapult->unitSide()), action);
	};

	ASSERT_TRUE(submitCatapultShot());
	const auto reduced = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	ASSERT_TRUE(reduced.used);
	ASSERT_EQ(reduced.activeUnitId, catapult->unitId());
	ASSERT_EQ(reduced.outputPercent, 50);
	const auto reducedPerHitOne = battle()->battleGetCatapultStructuralDamage(catapult, 1);
	const auto reducedPerHitTwo = battle()->battleGetCatapultStructuralDamage(catapult, 2);
	EXPECT_EQ(reducedPerHitOne, normalPerHit / 2);
	EXPECT_GT(reducedPerHitTwo, reducedPerHitOne);

	std::vector<std::pair<EWallPart, int32_t>> structuralBefore;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		const auto hp = battle()->getWallStructuralHP(part);
		if(hp > 0)
			structuralBefore.emplace_back(part, hp);
	}
	ASSERT_FALSE(structuralBefore.empty());
	ASSERT_TRUE(submitCatapultShot());

	int damagedParts = 0;
	for(const auto & [part, before] : structuralBefore)
	{
		const int32_t after = battle()->getWallStructuralHP(part);
		const int32_t damage = before - after;
		if(damage <= 0)
			continue;
		++damagedParts;
		const auto expectedQualityOne = std::min<int32_t>(before, reducedPerHitOne);
		const auto expectedQualityTwo = std::min<int32_t>(before, reducedPerHitTwo);
		EXPECT_TRUE(damage == expectedQualityOne || damage == expectedQualityTwo)
			<< "Accepted Catapult output should be scaled through the real structural packet";
	}
	EXPECT_GT(damagedParts, 0);
	const auto finished = battle()->battleGetReducedExtraActivationState(BattleSide::ATTACKER);
	EXPECT_TRUE(finished.used);
	EXPECT_FALSE(finished.hasActiveUnit());
	EXPECT_EQ(finished.outputPercent, 100);
	EXPECT_EQ(battle()->battleGetActivationOutputPercent(catapult), 100);

	const auto * ballista = machine(BattleSide::ATTACKER, ArtifactID::BALLISTA);
	ASSERT_NE(ballista, nullptr);
	auto * creatureTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10000);
	ASSERT_NE(creatureTarget, nullptr);
	advanceUntil(ballista, false);
	const auto beforeBallistaAction = std::count_if(server.stackActivations.begin(), server.stackActivations.end(),
		[](const BattleSetActiveStack & activation)
	{
		return activation.reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION;
	});
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, creatureTarget)));
	const auto afterBallistaAction = std::count_if(server.stackActivations.begin(), server.stackActivations.end(),
		[](const BattleSetActiveStack & activation)
	{
		return activation.reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION;
	});
	EXPECT_EQ(afterBallistaAction, beforeBallistaAction)
		<< "The used allowance must not grant a second machine its own extra activation";
}
