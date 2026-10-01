/*
 * NewHorizonsChainOfFortuneTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>

namespace
{
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorId = "new-horizons:luck.fortuneSFavor";
constexpr auto chainOfFortuneId = "new-horizons:luck.chainOfFortune";

JsonNode chanceCurve(int value)
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(value);
	return result;
}

class ChainOfFortuneEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ChainOfFortuneEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsChainOfFortuneTest : public BattleTestFixture
{
protected:
	int goodLuckChance = 100;
	int badLuckChance = 100;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, chanceCurve(goodLuckChance));
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, chanceCurve(badLuckChance));
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
	}

	void selectChainOfFortune(CGHeroInstance * hero)
	{
		const int decodedLuck = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decodedLuck, 0);
		const auto luck = SecondarySkill(decodedLuck);

		// Chain of Fortune is an Advanced Luck perk. Establish its Basic Luck
		// prerequisite and accept Fortune's Favor through a real legal offer first.
		hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, fortuneFavorId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, fortuneFavorId));
		hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, chainOfFortuneId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, chainOfFortuneId));
	}

	void acceptLuckPerkThroughOffer(CGHeroInstance * hero, const char * perkId)
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
				return candidate.selection.skillId == luckSkillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			EXPECT_TRUE(hero->hasActivePerk(luckSkillId, perkId));
			return;
		}

		FAIL() << perkId << " never appeared in a legal Luck perk offer";
	}

	CStack * addStack(BattleSide side, const CreatureID & creature, const BattleHex & position, int32_t count)
	{
		auto * stack = BattleTestFixture::addStack(side, creature, position, count);
		const auto * hero = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		if(hero && hero->hasActivePerk(luckSkillId, chainOfFortuneId))
		{
			// Advanced Luck is required for this perk and normally contributes +2.
			// Cancel only that ordinary skill value so each case measures the chain.
			stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::LUCK, BonusSource::OTHER, -2, BonusSourceID()));
		}
		return stack;
	}

	void addLuck(CStack * stack, int value)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LUCK, BonusSource::OTHER, value, BonusSourceID()));
	}

	void addNoLuck(CStack * stack)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	}

	bool submit(CStack * attacker, BattleAction action)
	{
		battle()->activeStack = attacker->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(attacker), action);
	}

	bool melee(CStack * attacker, CStack * target)
	{
		return submit(attacker, BattleAction::makeMeleeAttack(
			attacker, target->getPosition(), attacker->getPosition()));
	}

	bool shoot(CStack * attacker, CStack * target)
	{
		return submit(attacker, BattleAction::makeShotAttack(attacker, target));
	}
};
}

TEST(ChainOfFortuneRulesTest, StateAndAttackPacketRoundTripKeepLegacyDefaultsAndRejectInvalidState)
{
	SylvanLuckState state;
	state.chainOfFortune = true;
	state.chainTriggeredThisRound = true;
	state.chainSourceUnitId = 42;
	state.positiveLuckUnits.insert(42);
	EXPECT_TRUE(state.active());
	EXPECT_FALSE(state.chainFortuneAvailable(42));
	EXPECT_TRUE(state.chainFortuneAvailable(17));
	EXPECT_EQ(state.chanceLuck(0, 42, false), 0)
		<< "The stack that armed the pending benefit cannot receive it";
	EXPECT_EQ(state.chanceLuck(0, 17, false), 1);

	auto carried = state;
	carried.nextRound();
	EXPECT_FALSE(carried.chainTriggeredThisRound);
	ASSERT_TRUE(carried.chainSourceUnitId);
	EXPECT_EQ(*carried.chainSourceUnitId, 42);
	EXPECT_TRUE(carried.chainFortuneAvailable(17));
	EXPECT_FALSE(carried.recordStrike(42, true, false));
	EXPECT_TRUE(carried.chainTriggeredThisRound)
		<< "A positive same-origin strike in the new round spends that round's trigger allowance";
	ASSERT_TRUE(carried.chainSourceUnitId);
	EXPECT_EQ(*carried.chainSourceUnitId, 42)
		<< "The existing benefit remains a single pending gift instead of duplicating";
	EXPECT_TRUE(carried.chainFortuneAvailable(17));

	CMemorySerializer stateWire;
	stateWire.oser & state;
	SylvanLuckState restored;
	stateWire.iser & restored;
	EXPECT_EQ(restored, state);

	BattleAttack packet;
	packet.battleID = BattleID(0);
	packet.stackAttacking = 42;
	packet.fortuneSide = BattleSide::ATTACKER;
	packet.fortuneState = state;
	CMemorySerializer packetWire;
	packetWire.oser & packet;
	BattleAttack restoredPacket;
	packetWire.iser & restoredPacket;
	EXPECT_EQ(restoredPacket.fortuneSide, BattleSide::ATTACKER);
	ASSERT_TRUE(restoredPacket.fortuneState);
	EXPECT_EQ(*restoredPacket.fortuneState, state);

	CMemorySerializer rejectedStateDowngrade;
	rejectedStateDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_GAMBLER;
	EXPECT_THROW(rejectedStateDowngrade.oser & state, std::runtime_error);
	EXPECT_TRUE(rejectedStateDowngrade.extractBuffer().empty());

	CMemorySerializer rejectedPacketDowngrade;
	rejectedPacketDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_GAMBLER;
	EXPECT_THROW(rejectedPacketDowngrade.oser & packet, std::runtime_error);

	SylvanLuckState legacy;
	legacy.serendipity = true;
	CMemorySerializer legacyWire;
	legacyWire.oser.version = legacyWire.iser.version = ESerializationVersion::NEW_HORIZONS_GAMBLER;
	legacyWire.oser & legacy;
	SylvanLuckState restoredLegacy;
	restoredLegacy.chainOfFortune = true;
	restoredLegacy.chainTriggeredThisRound = true;
	restoredLegacy.chainSourceUnitId = 17;
	legacyWire.iser & restoredLegacy;
	EXPECT_TRUE(restoredLegacy.serendipity);
	EXPECT_FALSE(restoredLegacy.chainOfFortune);
	EXPECT_FALSE(restoredLegacy.chainTriggeredThisRound);
	EXPECT_FALSE(restoredLegacy.chainSourceUnitId);

	BattleAttack legacyPacket;
	legacyPacket.battleID = BattleID(0);
	legacyPacket.fortuneSide = BattleSide::ATTACKER;
	legacyPacket.fortuneState = legacy;
	CMemorySerializer legacyPacketWire;
	legacyPacketWire.oser.version = legacyPacketWire.iser.version = ESerializationVersion::NEW_HORIZONS_GAMBLER;
	legacyPacketWire.oser & legacyPacket;
	BattleAttack restoredLegacyPacket;
	legacyPacketWire.iser & restoredLegacyPacket;
	ASSERT_TRUE(restoredLegacyPacket.fortuneState);
	EXPECT_FALSE(restoredLegacyPacket.fortuneState->chainOfFortune)
		<< "Pre-Chain packets decode with an inert Chain of Fortune default";

	SylvanLuckState disabled;
	disabled.chainTriggeredThisRound = true;
	CMemorySerializer disabledWire;
	disabledWire.oser & disabled;
	SylvanLuckState restoredDisabled;
	EXPECT_THROW(disabledWire.iser & restoredDisabled, std::runtime_error);

	SylvanLuckState invalidOrigin;
	invalidOrigin.chainOfFortune = true;
	invalidOrigin.chainSourceUnitId = 42;
	CMemorySerializer invalidOriginWire;
	invalidOriginWire.oser & invalidOrigin;
	SylvanLuckState restoredInvalidOrigin;
	EXPECT_THROW(invalidOriginWire.iser & restoredInvalidOrigin, std::runtime_error);
}

TEST_F(NewHorizonsChainOfFortuneTest, CertainPositiveNativeStrikeArmsOneGiftForTheNextDifferentStack)
{
	startGame();
	selectChainOfFortune(attackerSideHero);
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 2), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	blockRetaliation(source);
	blockRetaliation(recipient);
	blockRetaliation(defender);
	addLuck(source, 1);
	beginCombat();

	ASSERT_EQ(battle()->battleGetAttackLuck(source, defender, false), 1);
	ASSERT_TRUE(melee(source, defender));
	ASSERT_FALSE(server.attacks.empty());
	ASSERT_TRUE(server.attacks.back().fortuneState);
	EXPECT_TRUE(server.attacks.back().lucky());
	EXPECT_EQ(server.attacks.back().fortuneSide, BattleSide::ATTACKER);
	EXPECT_TRUE(server.attacks.back().fortuneState->chainTriggeredThisRound);
	ASSERT_TRUE(server.attacks.back().fortuneState->chainSourceUnitId);
	EXPECT_EQ(*server.attacks.back().fortuneState->chainSourceUnitId, source->unitId());

	auto state = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_TRUE(state.chainOfFortune);
	EXPECT_TRUE(state.chainTriggeredThisRound);
	ASSERT_TRUE(state.chainSourceUnitId);
	EXPECT_EQ(*state.chainSourceUnitId, source->unitId());
	EXPECT_FALSE(state.chainFortuneAvailable(source->unitId()));
	EXPECT_TRUE(state.chainFortuneAvailable(recipient->unitId()));
	EXPECT_EQ(battle()->battleGetAttackLuck(recipient, defender, false), 1);

	server.attacks.clear();
	ASSERT_TRUE(melee(recipient, defender));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.back().lucky());
	ASSERT_TRUE(server.attacks.back().fortuneState);
	EXPECT_TRUE(server.attacks.back().fortuneState->chainTriggeredThisRound);
	EXPECT_FALSE(server.attacks.back().fortuneState->chainSourceUnitId);

	state = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_TRUE(state.chainTriggeredThisRound)
		<< "The recipient's strike cannot trigger a second benefit in the same round";
	EXPECT_FALSE(state.chainSourceUnitId)
		<< "The single pending benefit is consumed by the next different friendly stack";
}

TEST_F(NewHorizonsChainOfFortuneTest, MarksmanSecondArrowLeavesItsOwnPendingBenefitAvailable)
{
	startGame();
	selectChainOfFortune(attackerSideHero);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), BattleHex(rightHex + 5), 1000);
	blockRetaliation(shooter);
	blockRetaliation(target);
	addLuck(shooter, 1);
	beginCombat();

	ASSERT_EQ(battle()->battleGetAttackLuck(shooter, target, true), 1);
	ASSERT_TRUE(shoot(shooter, target));
	ASSERT_EQ(server.attacks.size(), 2u) << "Marksman resolves both arrows through the native attack path";
	for(const auto & attack : server.attacks)
	{
		EXPECT_TRUE(attack.lucky());
		ASSERT_TRUE(attack.fortuneState);
		ASSERT_TRUE(attack.fortuneState->chainSourceUnitId);
		EXPECT_EQ(*attack.fortuneState->chainSourceUnitId, shooter->unitId());
		EXPECT_TRUE(attack.fortuneState->chainTriggeredThisRound);
	}

	const auto state = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_TRUE(state.chainFortuneAvailable(shooter->unitId() + 1));
	EXPECT_FALSE(state.chainFortuneAvailable(shooter->unitId()));
	EXPECT_EQ(state.chainSourceUnitId, shooter->unitId());
}

TEST_F(NewHorizonsChainOfFortuneTest, PendingGiftCarriesAcrossRoundWhileTheTriggerAllowanceResets)
{
	startGame();
	selectChainOfFortune(attackerSideHero);
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 2), 100);
	auto * freshTrigger = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"),
		BattleHex(leftHex + GameConstants::BFIELD_WIDTH + 1), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	for(auto * stack : {source, recipient, freshTrigger, defender})
		blockRetaliation(stack);
	addLuck(source, 1);
	beginCombat();
	ASSERT_TRUE(melee(source, defender));
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).chainSourceUnitId);

	const auto previousRound = battle()->getRound();
	endRound();
	ASSERT_EQ(battle()->getRound(), previousRound + 1);
	auto state = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_FALSE(state.chainTriggeredThisRound);
	ASSERT_TRUE(state.chainSourceUnitId);
	EXPECT_EQ(*state.chainSourceUnitId, source->unitId());
	EXPECT_TRUE(state.chainFortuneAvailable(recipient->unitId()));
	EXPECT_EQ(battle()->battleGetAttackLuck(recipient, defender, false), 1);

	// The native roll uses GameRandomizer settings, while this battle curve is
	// used for shared chance calculations. Make this attack deterministically
	// non-positive through No Luck instead of mutating the curve after setup.
	// The carried +1 is still consumed by the first different stack that attacks.
	addNoLuck(recipient);
	EXPECT_EQ(battle()->battleGetAttackLuck(recipient, defender, false), 0);
	server.attacks.clear();
	ASSERT_TRUE(melee(recipient, defender));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.back().lucky());
	ASSERT_TRUE(server.attacks.back().fortuneState);
	EXPECT_FALSE(server.attacks.back().fortuneState->chainTriggeredThisRound);
	EXPECT_FALSE(server.attacks.back().fortuneState->chainSourceUnitId);

	state = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_FALSE(state.chainTriggeredThisRound);
	EXPECT_FALSE(state.chainSourceUnitId);

	// Once the carried token is gone, a positive trigger in the new round can
	// arm that round's fresh token under the 100% chance configured at setup.
	addLuck(freshTrigger, 1);
	server.attacks.clear();
	ASSERT_TRUE(melee(freshTrigger, defender));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.back().lucky());
	state = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_TRUE(state.chainTriggeredThisRound);
	ASSERT_TRUE(state.chainSourceUnitId);
	EXPECT_EQ(*state.chainSourceUnitId, freshTrigger->unitId());
}

TEST_F(NewHorizonsChainOfFortuneTest, HypnotizedReactionUsesItsCurrentControllerAndNoLuckConsumesPendingGift)
{
	startGame();
	selectChainOfFortune(defenderSideHero);
	startBattle();
	auto * initiator = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * hypnotized = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	auto * noLuckAttacker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(leftHex + GameConstants::BFIELD_WIDTH), 100);
	blockRetaliation(noLuckAttacker);
	const auto hypnotize = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	hypnotized->addNewBonus(hypnotize);
	addLuck(hypnotized, 1);
	addNoLuck(noLuckAttacker);
	beginCombat();

	ASSERT_EQ(battle()->battleGetOwner(hypnotized), PlayerColor(1));
	ASSERT_TRUE(hypnotized->ableToRetaliate());
	ASSERT_TRUE(melee(initiator, hypnotized));
	ASSERT_GE(server.attacks.size(), 2u);
	const auto & reaction = server.attacks.back();
	EXPECT_TRUE(reaction.counter());
	EXPECT_TRUE(reaction.lucky());
	EXPECT_EQ(reaction.stackAttacking, hypnotized->unitId());
	EXPECT_EQ(reaction.fortuneSide, BattleSide::DEFENDER)
		<< "A reaction uses the stack's current controlling army for Chain of Fortune";
	ASSERT_TRUE(reaction.fortuneState);
	EXPECT_TRUE(reaction.fortuneState->chainTriggeredThisRound);
	ASSERT_TRUE(reaction.fortuneState->chainSourceUnitId);
	EXPECT_EQ(*reaction.fortuneState->chainSourceUnitId, hypnotized->unitId());
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).chainTriggeredThisRound);

	auto defenderState = battle()->getSylvanLuckState(BattleSide::DEFENDER);
	ASSERT_TRUE(defenderState.chainFortuneAvailable(noLuckAttacker->unitId()));
	EXPECT_EQ(battle()->battleGetAttackLuck(noLuckAttacker, initiator, false), 0)
		<< "No Luck keeps its normal immunity even while the chain token is pending";

	server.attacks.clear();
	ASSERT_TRUE(melee(noLuckAttacker, initiator));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.front().lucky());
	ASSERT_TRUE(server.attacks.front().fortuneState);
	EXPECT_EQ(server.attacks.front().fortuneSide, BattleSide::DEFENDER);
	EXPECT_FALSE(server.attacks.front().fortuneState->chainSourceUnitId)
		<< "The next different friendly attack consumes the token despite No Luck";
	EXPECT_TRUE(server.attacks.front().fortuneState->chainTriggeredThisRound);

	defenderState = battle()->getSylvanLuckState(BattleSide::DEFENDER);
	EXPECT_FALSE(defenderState.chainSourceUnitId);
	EXPECT_TRUE(defenderState.chainTriggeredThisRound);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).chainTriggeredThisRound)
		<< "The other army's round allowance remains independent";
}

TEST_F(NewHorizonsChainOfFortuneTest, DetachedCandidateAndSelectedReplayConsumeOnlyTheirOwnPendingGift)
{
	startGame();
	selectChainOfFortune(attackerSideHero);
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 2), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	blockRetaliation(source);
	blockRetaliation(recipient);
	blockRetaliation(target);
	addLuck(source, 1);
	beginCombat();
	ASSERT_TRUE(melee(source, target));
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));

	auto environment = std::make_shared<ChainOfFortuneEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto root = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), root);
	auto projectedRecipient = branch->getForUpdate(recipient->unitId());
	auto projectedTarget = branch->getForUpdate(target->unitId());
	ASSERT_NE(projectedRecipient, nullptr);
	ASSERT_NE(projectedTarget, nullptr);
	BattleAttackInfo candidate(projectedRecipient.get(), projectedTarget.get(), 0, false);
	EXPECT_EQ(branch->battleGetAttackLuck(projectedRecipient.get(), projectedTarget.get(), false), 1);
	ASSERT_TRUE(branch->fortuneStrikeIsCertain(candidate));

	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(candidate, recipient->getPosition(), cache, branch);
	ASSERT_FALSE(prediction.fortuneStrikes.empty());
	ASSERT_TRUE(prediction.fortuneStrikes.front().resolvedLuck);
	EXPECT_EQ(*prediction.fortuneStrikes.front().resolvedLuck, ProjectedLuckOutcome::POSITIVE);
	ASSERT_NE(prediction.effectPreview, nullptr);
	const auto previewState = prediction.effectPreview->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_TRUE(previewState.chainTriggeredThisRound);
	EXPECT_FALSE(previewState.chainSourceUnitId)
		<< "The detached positive strike consumes the inherited gift without arming a duplicate";
	EXPECT_TRUE(branch->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));
	EXPECT_TRUE(root->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));

	// An uncertain candidate still consumes its pending gift in the detached
	// preview and selected replay, but it must not invent a positive outcome.
	battle()->luckRollRules.goodChance.assign(10, 50);
	battle()->luckRollRules.badChance.assign(10, 50);
	auto unknownRoot = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto unknownBranch = std::make_shared<HypotheticBattle>(environment.get(), unknownRoot);
	auto unknownRecipient = unknownBranch->getForUpdate(recipient->unitId());
	auto unknownTarget = unknownBranch->getForUpdate(target->unitId());
	ASSERT_NE(unknownRecipient, nullptr);
	ASSERT_NE(unknownTarget, nullptr);
	BattleAttackInfo unknownCandidate(unknownRecipient.get(), unknownTarget.get(), 0, false);
	EXPECT_FALSE(unknownBranch->fortuneStrikeIsCertain(unknownCandidate));

	DamageCache unknownCache;
	const auto unknownPrediction = AttackPossibility::evaluate(
		unknownCandidate, recipient->getPosition(), unknownCache, unknownBranch);
	ASSERT_FALSE(unknownPrediction.fortuneStrikes.empty());
	ASSERT_TRUE(unknownPrediction.fortuneStrikes.front().resolvedLuck);
	EXPECT_EQ(*unknownPrediction.fortuneStrikes.front().resolvedLuck, ProjectedLuckOutcome::UNKNOWN);
	ASSERT_NE(unknownPrediction.effectPreview, nullptr);
	const auto unknownPreviewState = unknownPrediction.effectPreview->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_FALSE(unknownPreviewState.chainSourceUnitId);
	EXPECT_FALSE(unknownPreviewState.positiveLuckUnits.contains(recipient->unitId()));
	EXPECT_TRUE(unknownRoot->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));

	auto unknownSelected = std::make_shared<HypotheticBattle>(environment.get(), unknownRoot);
	BattleExchangeVariant unknownCommitted;
	unknownCommitted.trackAttack(unknownPrediction, unknownSelected, unknownCache);
	const auto selectedUnknownState = unknownSelected->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_FALSE(selectedUnknownState.chainSourceUnitId);
	EXPECT_FALSE(selectedUnknownState.positiveLuckUnits.contains(recipient->unitId()));
	EXPECT_TRUE(unknownRoot->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));

	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), root);
	EXPECT_TRUE(sibling->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));
	auto selected = std::make_shared<HypotheticBattle>(environment.get(), root);
	BattleExchangeVariant committed;
	committed.trackAttack(prediction, selected, cache);
	EXPECT_FALSE(selected->getSylvanLuckState(BattleSide::ATTACKER).chainSourceUnitId)
		<< "Replaying the selected attack consumes only the selected branch's copy";
	EXPECT_TRUE(sibling->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));
	EXPECT_TRUE(root->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).chainFortuneAvailable(recipient->unitId()));
}
