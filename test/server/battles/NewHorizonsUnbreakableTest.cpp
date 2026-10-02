/*
 * NewHorizonsUnbreakableTest.cpp, part of VCMI engine
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
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/MoraleSuppressionState.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../include/vcmi/ServerCallback.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr auto disciplineSkillId = "new-horizons:discipline";
constexpr auto unbreakablePerkId = "new-horizons:discipline.unbreakable";
constexpr auto rallyPerkId = "new-horizons:discipline.rally";
constexpr auto inspirationalLeaderPerkId = "new-horizons:discipline.inspirationalLeader";
constexpr auto fearlessPerkId = "new-horizons:discipline.fearless";
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorPerkId = "new-horizons:luck.fortuneSFavor";
constexpr auto chainOfFortunePerkId = "new-horizons:luck.chainOfFortune";
constexpr auto twistOfFatePerkId = "new-horizons:luck.twistOfFate";

JsonNode moraleChance(int chance)
{
	JsonNode result;
	for(int index = 0; index < 10; ++index)
		result.Vector().emplace_back(chance);
	return result;
}

bool setPerkActive(JsonNode & rules, std::string_view skillId, std::string_view perkId)
{
	auto & perks = rules["skills"][std::string(skillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = "active";
	return true;
}

class UnbreakableTestEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit UnbreakableTestEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsUnbreakableTest : public BattleTestFixture
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
		if(!setPerkActive(perkRules, disciplineSkillId, unbreakablePerkId)
			|| !setPerkActive(perkRules, disciplineSkillId, rallyPerkId)
			|| !setPerkActive(perkRules, disciplineSkillId, inspirationalLeaderPerkId)
			|| !setPerkActive(perkRules, disciplineSkillId, fearlessPerkId)
			|| !setPerkActive(perkRules, luckSkillId, fortuneFavorPerkId)
			|| !setPerkActive(perkRules, luckSkillId, chainOfFortunePerkId)
			|| !setPerkActive(perkRules, luckSkillId, twistOfFatePerkId))
			throw std::runtime_error("Missing Unbreakable, Rally, or Twist of Fate from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_MORALE_CHANCE, moraleChance(badMoraleChancePercent));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, moraleChance(goodMoraleChancePercent));
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, const std::string & skillId, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & lookupSkillId)
		{
			return hero->getPerkSkillRank(lookupSkillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&](const auto & candidate)
			{
				return candidate.selection.skillId == skillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			EXPECT_TRUE(hero->hasActivePerk(skillId, perkId));
			return;
		}

		FAIL() << perkId << " never appeared in a legal perk offer";
	}

	void selectUnbreakable(CGHeroInstance * hero, bool alsoRally)
	{
		const int decodedDiscipline = SecondarySkill::decode(disciplineSkillId);
		ASSERT_GE(decodedDiscipline, 0);
		const SecondarySkill discipline(decodedDiscipline);
		hero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, disciplineSkillId,
			alsoRally ? rallyPerkId : inspirationalLeaderPerkId);
		hero->setSecSkillLevel(discipline, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, disciplineSkillId, fearlessPerkId);
		hero->setSecSkillLevel(discipline, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, disciplineSkillId, unbreakablePerkId);
		ASSERT_TRUE(hero->hasActivePerk(disciplineSkillId, unbreakablePerkId));
	}

	void selectTwistOfFate(CGHeroInstance * hero)
	{
		const int decodedLuck = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decodedLuck, 0);
		const SecondarySkill luck(decodedLuck);

		hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, fortuneFavorPerkId);
		hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, chainOfFortunePerkId);
		hero->setSecSkillLevel(luck, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, twistOfFatePerkId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, twistOfFatePerkId));
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void startUnbreakableBattle(BattleSide skillSide, bool alsoRally, bool withTwist = false,
		int badChance = 100, int goodChance = 0)
	{
		badMoraleChancePercent = badChance;
		goodMoraleChancePercent = goodChance;
		startGame();
		auto * skillHero = skillSide == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		selectUnbreakable(skillHero, alsoRally);
		if(withTwist)
			selectTwistOfFate(skillHero);

		startBattle();
		EXPECT_EQ(battle()->getMoraleSuppressionState(skillSide).roundEnabled, true);
		EXPECT_EQ(battle()->getMoraleSuppressionState(skillSide).enabled, alsoRally);
		const auto otherSide = skillSide == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		EXPECT_FALSE(battle()->getMoraleSuppressionState(otherSide).roundEnabled);
		removeDeployedUnits();
	}

	CStack * addNegativeMoraleStack(BattleSide side, const CreatureID & creature,
		const BattleHex & position, int morale = -20)
	{
		auto * stack = addStack(side, creature, position, 10);
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MORALE, BonusSource::OTHER, morale, BonusSourceID()));
		return stack;
	}

	void defendActiveStack()
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
	}

	static size_t activationCount(const std::vector<BattleSetActiveStack> & activations,
		uint32_t unitId, BattleUnitTurnReason reason)
	{
		return static_cast<size_t>(std::count_if(activations.begin(), activations.end(),
			[unitId, reason](const auto & activation)
			{
				return activation.stack == unitId && activation.reason == reason;
			}));
	}

	size_t trackedActivationCount(const std::vector<const CStack *> & stacks,
		BattleUnitTurnReason reason) const
	{
		size_t result = 0;
		for(const auto * stack : stacks)
			result += activationCount(server.stackActivations, stack->unitId(), reason);
		return result;
	}

	void advanceUntilTrackedActivations(const std::vector<const CStack *> & stacks, size_t expectedCount)
	{
		for(int attempt = 0; attempt < 48; ++attempt)
		{
			const auto tracked = trackedActivationCount(stacks, BattleUnitTurnReason::TURN_QUEUE)
				+ trackedActivationCount(stacks, BattleUnitTurnReason::AUTOMATIC_ACTION);
			if(tracked >= expectedCount)
				return;
			ASSERT_NE(battle()->battleActiveUnit(), nullptr);
			defendActiveStack();
		}
		FAIL() << "The expected negative-Morale activations did not resolve";
	}

	void advanceUntilActivationReason(const CStack * stack, BattleUnitTurnReason reason)
	{
		for(int attempt = 0; attempt < 48; ++attempt)
		{
			if(activationCount(server.stackActivations, stack->unitId(), reason) > 0)
				return;
			ASSERT_NE(battle()->battleActiveUnit(), nullptr);
			defendActiveStack();
		}
		FAIL() << "The expected stack activation reason was not recorded";
	}

	void advanceUntilActive(const CStack * expected)
	{
		for(int attempt = 0; attempt < 24; ++attempt)
		{
			if(battle()->battleActiveUnit() == expected)
				return;
			ASSERT_NE(battle()->battleActiveUnit(), nullptr);
			defendActiveStack();
		}
		FAIL() << "The expected stack did not receive an ordinary activation";
	}

	int badMoraleChancePercent = 100;
	int goodMoraleChancePercent = 0;
};
}

TEST_F(NewHorizonsUnbreakableTest, IgnoresOneNegativeTriggerPerRoundAndRenewsAfterTheRound)
{
	startUnbreakableBattle(BattleSide::ATTACKER, false);
	auto * first = addNegativeMoraleStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex));
	auto * second = addNegativeMoraleStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 7));
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 20);
	const std::vector<const CStack *> negativeStacks{first, second};

	ASSERT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundAvailable());
	beginCombat();
	advanceUntilTrackedActivations(negativeStacks, 2);

	const auto firstRoundQueued = trackedActivationCount(negativeStacks, BattleUnitTurnReason::TURN_QUEUE);
	const auto firstRoundAutomatic = trackedActivationCount(negativeStacks, BattleUnitTurnReason::AUTOMATIC_ACTION);
	EXPECT_EQ(firstRoundQueued, 1u) << "The first negative trigger is ignored and the stack receives its normal activation";
	EXPECT_EQ(firstRoundAutomatic, 1u) << "A later negative trigger in the same round is not ignored";
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundUsed);
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).used);

	const auto firstRound = battle()->getRound();
	endRound();
	ASSERT_GT(battle()->getRound(), firstRound);
	advanceUntilTrackedActivations(negativeStacks, 4);
	EXPECT_EQ(trackedActivationCount(negativeStacks, BattleUnitTurnReason::TURN_QUEUE) - firstRoundQueued, 1u);
	EXPECT_EQ(trackedActivationCount(negativeStacks, BattleUnitTurnReason::AUTOMATIC_ACTION) - firstRoundAutomatic, 1u);
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundUsed)
		<< "The first negative trigger after the round boundary consumes the renewed allowance";
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).used)
		<< "Unbreakable does not turn its per-round use into Rally's battle-long use";
}

TEST_F(NewHorizonsUnbreakableTest, UnbreakableAndRallyProtectSeparateTriggersBeforeTwistOfFate)
{
	startUnbreakableBattle(BattleSide::DEFENDER, true, true);
	auto * hypnotized = addNegativeMoraleStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex));
	hypnotized->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	auto * firstPikeman = addNegativeMoraleStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(12, 4));
	auto * secondPikeman = addNegativeMoraleStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(12, 6));
	const std::vector<const CStack *> pikemen{firstPikeman, secondPikeman};

	ASSERT_EQ(hypnotized->unitSide(), BattleSide::ATTACKER);
	beginCombat();
	advanceUntilActive(hypnotized);
	ASSERT_EQ(battle()->battleGetOwner(hypnotized), PlayerColor(1));
	ASSERT_EQ(battle()->playerToSide(battle()->battleGetOwner(hypnotized)), BattleSide::DEFENDER);
	EXPECT_EQ(activationCount(server.stackActivations, hypnotized->unitId(), BattleUnitTurnReason::TURN_QUEUE), 1u);
	EXPECT_EQ(activationCount(server.stackActivations, hypnotized->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION), 0u);
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::DEFENDER).roundUsed)
		<< "Unbreakable belongs to the stack's current controller, including a hypnotized enemy stack";
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::DEFENDER).used)
		<< "The same negative trigger spends only Unbreakable, leaving Rally available";
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundUsed);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available())
		<< "The ignored trigger returns before Twist of Fate can spend its reroll";

	advanceUntilTrackedActivations(pikemen, 1);
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::DEFENDER).used)
		<< "Rally protects the next trigger after this round's Unbreakable allowance is spent";
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());
	advanceUntilTrackedActivations(pikemen, 2);
	EXPECT_EQ(trackedActivationCount(pikemen, BattleUnitTurnReason::TURN_QUEUE), 1u);
	EXPECT_EQ(trackedActivationCount(pikemen, BattleUnitTurnReason::AUTOMATIC_ACTION), 1u)
		<< "A third negative trigger reaches the ordinary bad-Morale path after both allowances are spent";
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available())
		<< "At a deterministic 100% bad-Morale chance, the unsuppressed third trigger does not invoke a stochastic Twist reroll";
}

TEST_F(NewHorizonsUnbreakableTest, PositiveMoraleAndMoraleImmunityLeaveUnbreakableAvailable)
{
	startUnbreakableBattle(BattleSide::ATTACKER, false, false, 100, 100);
	auto * positive = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex), 10);
	positive->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MORALE, BonusSource::OTHER, 1, BonusSourceID()));
	auto * immune = addNegativeMoraleStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(5, 7));
	immune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_MORALE, BonusSource::OTHER, 0, BonusSourceID()));
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 20);

	ASSERT_GT(battle()->battleGetMorale(positive), 0);
	ASSERT_EQ(battle()->battleGetMorale(immune), 0);
	beginCombat();
	advanceUntilActive(positive);
	// Defend deliberately suppresses positive Morale. Exercise an ordinary
	// legal movement action so the guaranteed positive roll can actually occur.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetOwner(positive), BattleAction::makeMove(positive, BattleHex(4, 4))));
	advanceUntilActivationReason(positive, BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundUsed)
		<< "Positive Morale is not a negative trigger";
	advanceUntilActive(immune);
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundUsed)
		<< "NO_MORALE prevents a negative trigger from reaching Unbreakable";
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundAvailable());

	MoraleSuppressionState inactive;
	EXPECT_FALSE(inactive.consume(true)) << "An inactive side cannot spend an unenabled allowance";
	EXPECT_EQ(inactive, MoraleSuppressionState{});
	MoraleSuppressionState unbreakableOnly;
	unbreakableOnly.roundEnabled = true;
	EXPECT_TRUE(unbreakableOnly.consume(true));
	EXPECT_TRUE(unbreakableOnly.roundUsed);
	EXPECT_FALSE(unbreakableOnly.used) << "The Unbreakable gate is independent of Rally's gate";
}

TEST_F(NewHorizonsUnbreakableTest, ExpertDisciplineWithoutTheSelectedPerkDoesNotEnableUnbreakable)
{
	badMoraleChancePercent = 100;
	startGame();
	const int decodedDiscipline = SecondarySkill::decode(disciplineSkillId);
	ASSERT_GE(decodedDiscipline, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(decodedDiscipline),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundEnabled)
		<< "Expert rank alone does not activate an unselected Unbreakable perk";
	removeDeployedUnits();

	auto * negative = addNegativeMoraleStack(BattleSide::ATTACKER,
		creatureByName("core:archangel"), BattleHex(leftHex));
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 20);
	beginCombat();
	advanceUntilTrackedActivations({negative}, 1);
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundUsed);
	EXPECT_EQ(activationCount(server.stackActivations, negative->unitId(),
		BattleUnitTurnReason::AUTOMATIC_ACTION), 1u)
		<< "The ordinary negative-Morale failure remains when the Expert perk was not selected";
}

TEST_F(NewHorizonsUnbreakableTest, StateAndPacketSerializationPreserveOldRallySavesAndNewRoundFields)
{
	MoraleSuppressionState complete;
	complete.enabled = true;
	complete.used = true;
	complete.roundEnabled = true;
	complete.roundUsed = true;
	complete.validate();

	CMemorySerializer currentWire;
	currentWire.oser.version = ESerializationVersion::NEW_HORIZONS_UNBREAKABLE;
	currentWire.iser.version = ESerializationVersion::NEW_HORIZONS_UNBREAKABLE;
	currentWire.oser & complete;
	MoraleSuppressionState restored;
	currentWire.iser & restored;
	EXPECT_EQ(restored, complete);

	MoraleSuppressionState rallyOnly;
	rallyOnly.enabled = true;
	rallyOnly.used = true;
	CMemorySerializer rallyWire;
	rallyWire.oser.version = ESerializationVersion::NEW_HORIZONS_RALLY;
	rallyWire.iser.version = ESerializationVersion::NEW_HORIZONS_RALLY;
	rallyWire.oser & rallyOnly;
	MoraleSuppressionState restoredRally;
	rallyWire.iser & restoredRally;
	EXPECT_EQ(restoredRally, rallyOnly);
	EXPECT_FALSE(restoredRally.roundEnabled);
	EXPECT_FALSE(restoredRally.roundUsed)
		<< "A save written before Unbreakable retains Rally while defaulting its new state";

	CMemorySerializer rejectedDowngrade;
	rejectedDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_RALLY;
	EXPECT_THROW(rejectedDowngrade.oser & complete, std::runtime_error);

	MoraleSuppressionState fresh;
	fresh.enabled = true;
	fresh.roundEnabled = true;
	MoraleSuppressionState roundSpent = fresh;
	ASSERT_TRUE(roundSpent.consume(true));
	BattleMoraleSuppressionStateChanged packet;
	packet.battleID = BattleID(0);
	packet.side = BattleSide::ATTACKER;
	packet.state = roundSpent;
	EXPECT_NO_THROW(packet.validateTransitionFrom(fresh));

	MoraleSuppressionState afterRally = roundSpent;
	ASSERT_TRUE(afterRally.consume(true));
	packet.state = afterRally;
	EXPECT_NO_THROW(packet.validateTransitionFrom(roundSpent));
	MoraleSuppressionState doubleSpent = fresh;
	ASSERT_TRUE(doubleSpent.consume(true));
	ASSERT_TRUE(doubleSpent.consume(true));
	packet.state = doubleSpent;
	EXPECT_THROW(packet.validateTransitionFrom(fresh), std::runtime_error)
		<< "One negative trigger cannot spend both allowances in a single replicated update";

	packet.state = roundSpent;
	CMemorySerializer packetWire;
	packetWire.oser.version = ESerializationVersion::NEW_HORIZONS_UNBREAKABLE;
	packetWire.iser.version = ESerializationVersion::NEW_HORIZONS_UNBREAKABLE;
	packetWire.oser & packet;
	BattleMoraleSuppressionStateChanged restoredPacket;
	packetWire.iser & restoredPacket;
	EXPECT_EQ(restoredPacket.state, roundSpent);
	EXPECT_EQ(restoredPacket.side, BattleSide::ATTACKER);
}

TEST_F(NewHorizonsUnbreakableTest, DetachedBranchesCopySpendAndResetOnlyTheirRoundAllowance)
{
	startUnbreakableBattle(BattleSide::ATTACKER, true);
	ASSERT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundAvailable());
	ASSERT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).available());
	auto * original = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex), 10);
	UnbreakableTestEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto projected = branch->getForUpdate(original->unitId());
	ASSERT_NE(projected, nullptr);

	EXPECT_FLOAT_EQ(branch->projectMoraleActivationDelta(original, projected.get(), -0.35f, -0.70f, 2.0f), -0.35f)
		<< "The detached branch models Unbreakable's first event";
	EXPECT_TRUE(branch->getMoraleSuppressionState(BattleSide::ATTACKER).roundUsed);
	EXPECT_FALSE(branch->getMoraleSuppressionState(BattleSide::ATTACKER).used)
		<< "The first detached event does not also spend Rally";
	EXPECT_FLOAT_EQ(branch->projectMoraleActivationDelta(original, projected.get(), -0.35f, -0.70f, 2.0f), -0.35f)
		<< "The next event in the same round falls through to Rally";
	EXPECT_TRUE(branch->getMoraleSuppressionState(BattleSide::ATTACKER).used);
	EXPECT_FLOAT_EQ(branch->projectMoraleActivationDelta(original, projected.get(), -0.35f, -0.70f, 2.0f), -0.70f)
		<< "A third event in the same round has no suppression left";

	branch->nextRound();
	EXPECT_FALSE(branch->getMoraleSuppressionState(BattleSide::ATTACKER).roundUsed);
	EXPECT_TRUE(branch->getMoraleSuppressionState(BattleSide::ATTACKER).used)
		<< "Round reset renews Unbreakable without restoring Rally";
	EXPECT_FLOAT_EQ(branch->projectMoraleActivationDelta(original, projected.get(), -0.35f, -0.70f, 2.0f), -0.35f)
		<< "The renewed detached-round allowance protects its first event";

	EXPECT_TRUE(parent->getMoraleSuppressionState(BattleSide::ATTACKER).roundAvailable());
	EXPECT_TRUE(parent->getMoraleSuppressionState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(sibling->getMoraleSuppressionState(BattleSide::ATTACKER).roundAvailable());
	EXPECT_TRUE(sibling->getMoraleSuppressionState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).roundAvailable());
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).available());
}
