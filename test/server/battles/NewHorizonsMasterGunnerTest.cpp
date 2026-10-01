/*
 * NewHorizonsMasterGunnerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleHex.h"
#include "../../../lib/battle/BattleUnitTurnReason.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
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
constexpr auto masterGunnerPerkId = "new-horizons:warMachines.masterGunner";
constexpr auto rangedFollowUpStateKey = "rangedFollowUpDamagePercent";

bool setPerkActive(JsonNode & rules, std::string_view perkId)
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

class MasterGunnerEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MasterGunnerEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsMasterGunnerTest : public BattleTestFixture
{
protected:
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
		if(!setPerkActive(perkRules, masterGunnerPerkId))
			throw std::runtime_error("Missing Master Gunner from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		// Exercise the accepted capability snapshot rather than manufacturing a legacy version.
		JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, std::move(capabilities));
	}

	SecondarySkill warMachines() const
	{
		const int decoded = SecondarySkill::decode(warMachinesSkillId);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptMasterGunnerThroughOffer(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == warMachinesSkillId
					&& candidate.selection.perkId == masterGunnerPerkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(warMachinesSkillId, masterGunnerPerkId));
			return;
		}

		FAIL() << "No legal New Horizons perk offer contained " << masterGunnerPerkId;
	}

	void prepareBasicWarMachines(CGHeroInstance * hero, bool selectMasterGunner)
	{
		const auto skill = warMachines();
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(warMachinesSkillId,
			hero->getSecSkillLevel(skill)));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(warMachinesSkillId), MasteryLevel::BASIC);

		if(selectMasterGunner)
			acceptMasterGunnerThroughOffer(hero);
		else
			ASSERT_FALSE(hero->hasActivePerk(warMachinesSkillId, masterGunnerPerkId));
	}

	void prepareBallistaBattle(bool selectMasterGunner)
	{
		startGame();
		prepareBasicWarMachines(attackerSideHero, selectMasterGunner);
		giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
		startBattle();
	}

	const CStack * ballistaFor(BattleSide side) const
	{
		const auto machines = battle()->battleGetStacksIf([side](const CStack * stack)
		{
			return stack->unitSide() == side && stack->isBallista();
		});
		return machines.size() == 1 ? machines.front() : nullptr;
	}

	void advanceUntilBallista(const CStack * ballista)
	{
		beginCombat();
		const auto firstRound = battle()->getRound();
		const auto maximumTurns = battle()->stacks.size() * 2;
		for(size_t turn = 0; turn < maximumTurns && battle()->battleActiveUnit() != ballista; ++turn)
		{
			ASSERT_EQ(battle()->getRound(), firstRound);
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_NE(active->unitId(), ballista->unitId());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
		}
		ASSERT_EQ(battle()->battleActiveUnit(), ballista);
	}

	std::vector<const BattleAttack *> shotsBy(const CStack * stack) const
	{
		std::vector<const BattleAttack *> result;
		for(const auto & attack : server.attacks)
			if(attack.stackAttacking == stack->unitId() && attack.shot())
				result.push_back(&attack);
		return result;
	}

	static int64_t recordedDamage(const BattleAttack & attack)
	{
		int64_t total = 0;
		for(const auto & hit : attack.bsa)
			total += hit.damageAmount;
		return total;
	}

	static void expectSameDamage(const DamageRange & actual, const DamageRange & expected)
	{
		EXPECT_EQ(actual.min, expected.min);
		EXPECT_EQ(actual.max, expected.max);
	}
};
}

TEST_F(NewHorizonsMasterGunnerTest, LegalBasicPerkFiresTwoDifferentTargetShotsAtSharedSixtyPercentForecast)
{
	prepareBallistaBattle(true);
	const auto * ballista = ballistaFor(BattleSide::ATTACKER);
	ASSERT_NE(ballista, nullptr);
	ASSERT_EQ(battle()->battleGetOwnerHero(ballista), attackerSideHero);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, masterGunnerPerkId));

	auto * firstTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	auto * secondTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 2), 1000);
	auto * ordinaryShooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex + 2), 20);
	ASSERT_NE(firstTarget, nullptr);
	ASSERT_NE(secondTarget, nullptr);
	ASSERT_NE(ordinaryShooter, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, firstTarget->getPosition()));
	ASSERT_TRUE(battle()->battleCanShoot(ballista, secondTarget->getPosition()));
	ASSERT_FALSE(ordinaryShooter->isBallista());

	advanceUntilBallista(ballista);
	// The accepted Defend actions above change target defense. Capture the normal
	// shot baseline only after reaching the Ballista, so the later pending-shot
	// forecast differs solely by Master Gunner's final damage multiplier.
	const auto ordinaryForecast = battle()->calculateDmgRange(BattleAttackInfo(ballista, secondTarget, 0, true)).damage;
	ASSERT_GT(ordinaryForecast.max, 0);

	BattleClientInterfaceData clientData{};
	const auto availableActions = battle()->getClientActionsForStack(ballista, clientData);
	EXPECT_TRUE(std::ranges::any_of(availableActions, [](const PossiblePlayerBattleAction & action)
	{
		return action.get() == PossiblePlayerBattleAction::SHOOT;
	})) << "Basic War Machines should expose the existing manual Ballista Shoot action";

	const auto activationSerial = battle()->getActivationSerial();
	const auto firstShotForecast = battle()->calculateDmgRange(BattleAttackInfo(ballista, firstTarget, 0, true)).damage;
	const auto firstHealthBefore = firstTarget->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, firstTarget)));
	const auto firstDamage = firstHealthBefore - firstTarget->getAvailableHealth();
	EXPECT_GE(firstDamage, firstShotForecast.min);
	EXPECT_LE(firstDamage, firstShotForecast.max);
	ASSERT_TRUE(battle()->battleHasPendingRangedFollowUp(ballista));
	EXPECT_EQ(battle()->battleGetRangedFollowUpDamagePercent(ballista), 60);
	EXPECT_EQ(battle()->battleActiveUnit(), ballista);
	EXPECT_EQ(battle()->getActivationSerial(), activationSerial)
		<< "Continuing to the second shot must not begin a new Ballista activation";
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(ordinaryShooter));

	const auto continuation = std::find_if(server.stackActivations.rbegin(), server.stackActivations.rend(),
		[ballista](const BattleSetActiveStack & activation)
	{
		return activation.battleID == BattleID(0) && activation.stack == ballista->unitId()
			&& activation.reason == BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION;
	});
	ASSERT_NE(continuation, server.stackActivations.rend());

	const auto pendingForecast = battle()->calculateDmgRange(BattleAttackInfo(ballista, secondTarget, 0, true)).damage;
	EXPECT_NEAR(pendingForecast.min, ordinaryForecast.min * 0.6, 1.0);
	EXPECT_NEAR(pendingForecast.max, ordinaryForecast.max * 0.6, 1.0);
	EXPECT_LT(pendingForecast.max, ordinaryForecast.max);

	const auto liveState = ballista->acquireState();
	ASSERT_EQ(liveState->rangedFollowUpDamagePercent, 60);
	auto copiedState = ballista->acquireState();
	*copiedState = *liveState;
	EXPECT_EQ(copiedState->rangedFollowUpDamagePercent, 60);
	const auto savedUnitState = liveState->save();
	EXPECT_EQ(savedUnitState["state"][rangedFollowUpStateKey].Integer(), 60);
	auto restoredUnitState = ballista->acquireState();
	restoredUnitState->load(savedUnitState);
	EXPECT_EQ(restoredUnitState->rangedFollowUpDamagePercent, 60);
	auto legacyUnitState = savedUnitState;
	legacyUnitState["state"].Struct().erase(rangedFollowUpStateKey);
	restoredUnitState->load(legacyUnitState);
	EXPECT_EQ(restoredUnitState->rangedFollowUpDamagePercent, 0)
		<< "Older unit-state JSON has no pending follow-up";

	MasterGunnerEnvironment environment(gameState());
	const auto controller = battle()->sideToPlayer(ballista->unitSide());
	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), controller);
	HypotheticBattle projected(&environment, callback);
	const auto projectedBallista = projected.getForUpdate(ballista->unitId());
	const auto projectedTarget = projected.getForUpdate(secondTarget->unitId());
	ASSERT_NE(projectedBallista, nullptr);
	ASSERT_NE(projectedTarget, nullptr);
	ASSERT_TRUE(projected.battleHasPendingRangedFollowUp(projectedBallista.get()));
	EXPECT_EQ(projected.battleGetRangedFollowUpDamagePercent(projectedBallista.get()), 60);
	const auto projectedForecast = projected.calculateDmgRange(
		BattleAttackInfo(projectedBallista.get(), projectedTarget.get(), 0, true)).damage;
	expectSameDamage(projectedForecast, pendingForecast);

	CMemorySerializer continuationWire;
	continuationWire.oser.version = ESerializationVersion::CURRENT;
	continuationWire.iser.version = ESerializationVersion::CURRENT;
	continuationWire.oser & *continuation;
	BattleSetActiveStack restoredContinuation;
	continuationWire.iser & restoredContinuation;
	EXPECT_EQ(restoredContinuation.battleID, continuation->battleID);
	EXPECT_EQ(restoredContinuation.stack, ballista->unitId());
	EXPECT_EQ(restoredContinuation.reason, BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION);
	CMemorySerializer rejectedContinuationDowngrade;
	rejectedContinuationDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_PHYSICAL_AFFLICTIONS;
	EXPECT_THROW(rejectedContinuationDowngrade.oser & *continuation, std::runtime_error);
	EXPECT_TRUE(rejectedContinuationDowngrade.extractBuffer().empty());

	const auto secondShotForecast = battle()->calculateDmgRange(BattleAttackInfo(ballista, secondTarget, 0, true)).damage;
	const auto secondHealthBefore = secondTarget->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, secondTarget)));
	const auto secondDamage = secondHealthBefore - secondTarget->getAvailableHealth();
	EXPECT_GE(secondDamage, secondShotForecast.min);
	EXPECT_LE(secondDamage, secondShotForecast.max);
	EXPECT_NE(battle()->battleActiveUnit(), ballista)
		<< "The accepted second shot should finish the original activation";
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(ballista))
		<< "The accepted second shot consumes rather than rearms the allowance";
	EXPECT_EQ(ballista->acquireState()->rangedFollowUpDamagePercent, 0);

	const auto ballistaShots = shotsBy(ballista);
	ASSERT_EQ(ballistaShots.size(), 2u);
	ASSERT_EQ(ballistaShots[0]->bsa.size(), 1u);
	ASSERT_EQ(ballistaShots[1]->bsa.size(), 1u);
	EXPECT_EQ(ballistaShots[0]->bsa.front().stackAttacked, firstTarget->unitId());
	EXPECT_EQ(ballistaShots[1]->bsa.front().stackAttacked, secondTarget->unitId());
	EXPECT_GT(recordedDamage(*ballistaShots[1]), 0);
}

TEST_F(NewHorizonsMasterGunnerTest, PendingShotCanFinishAfterTheHeroLosesWarMachinesRank)
{
	prepareBallistaBattle(true);
	const auto * ballista = ballistaFor(BattleSide::ATTACKER);
	ASSERT_NE(ballista, nullptr);
	auto * firstTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	auto * secondTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 2), 1000);
	ASSERT_NE(firstTarget, nullptr);
	ASSERT_NE(secondTarget, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, firstTarget->getPosition()));
	ASSERT_TRUE(battle()->battleCanShoot(ballista, secondTarget->getPosition()));
	advanceUntilBallista(ballista);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, firstTarget)));
	ASSERT_TRUE(battle()->battleHasPendingRangedFollowUp(ballista));
	EXPECT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, masterGunnerPerkId));

	gameHandler->changeSecSkill(attackerSideHero, warMachines(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(warMachinesSkillId, masterGunnerPerkId));
	EXPECT_EQ(battle()->battleGetRangedFollowUpDamagePercent(ballista), 60)
		<< "An earned continuation is state-backed and should not be revalidated against the perk";
	const auto followUpForecast = battle()->calculateDmgRange(BattleAttackInfo(ballista, secondTarget, 0, true)).damage;
	const auto healthBefore = secondTarget->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, secondTarget)));
	const auto actualDamage = healthBefore - secondTarget->getAvailableHealth();
	EXPECT_GE(actualDamage, followUpForecast.min);
	EXPECT_LE(actualDamage, followUpForecast.max);
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(ballista));
	EXPECT_EQ(shotsBy(ballista).size(), 2u);
}

TEST_F(NewHorizonsMasterGunnerTest, RejectedShotRetainsAllowanceAndNoActionDeclinesIt)
{
	prepareBallistaBattle(true);
	const auto * ballista = ballistaFor(BattleSide::ATTACKER);
	ASSERT_NE(ballista, nullptr);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1000);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NE(friendly, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, enemy->getPosition()));
	advanceUntilBallista(ballista);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, enemy)));
	ASSERT_TRUE(battle()->battleHasPendingRangedFollowUp(ballista));
	ASSERT_EQ(battle()->battleGetRangedFollowUpDamagePercent(ballista), 60);
	ASSERT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, friendly)));
	EXPECT_TRUE(battle()->battleHasPendingRangedFollowUp(ballista));
	EXPECT_EQ(battle()->battleGetRangedFollowUpDamagePercent(ballista), 60);
	EXPECT_EQ(battle()->battleActiveUnit(), ballista);
	EXPECT_EQ(shotsBy(ballista).size(), 1u);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeNoAction(ballista)));
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(ballista));
	EXPECT_EQ(ballista->acquireState()->rangedFollowUpDamagePercent, 0);
	EXPECT_EQ(shotsBy(ballista).size(), 1u);
	EXPECT_NE(battle()->battleActiveUnit(), ballista)
		<< "Explicitly declining the follow-up safely finishes the original activation";
}

TEST_F(NewHorizonsMasterGunnerTest, BasicWarMachinesWithoutSavedPerkAndOrdinaryShootersGetNoFollowUp)
{
	prepareBallistaBattle(false);
	const auto * ballista = ballistaFor(BattleSide::ATTACKER);
	ASSERT_NE(ballista, nullptr);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(warMachinesSkillId, masterGunnerPerkId));
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	auto * ordinaryShooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex + 2), 20);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NE(ordinaryShooter, nullptr);
	ASSERT_FALSE(ordinaryShooter->isBallista());
	ASSERT_TRUE(battle()->battleCanShoot(ballista, enemy->getPosition()));
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(ordinaryShooter));
	advanceUntilBallista(ballista);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, enemy)));
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(ballista));
	EXPECT_EQ(ballista->acquireState()->rangedFollowUpDamagePercent, 0);
	EXPECT_EQ(shotsBy(ballista).size(), 1u);
}
