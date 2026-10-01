/*
 * NewHorizonsRallyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in the main folder
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
#include <optional>
#include <string>
#include <string_view>

namespace
{
constexpr auto disciplineSkillId = "new-horizons:discipline";
constexpr auto rallyPerkId = "new-horizons:discipline.rally";
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorId = "new-horizons:luck.fortuneSFavor";
constexpr auto chainOfFortuneId = "new-horizons:luck.chainOfFortune";
constexpr auto twistOfFateId = "new-horizons:luck.twistOfFate";

JsonNode badMoraleChance(int chance)
{
	JsonNode result;
	for(int index = 0; index < 10; ++index)
		result.Vector().emplace_back(chance);
	return result;
}

bool setPerkStatus(JsonNode & rules, std::string_view skillId, std::string_view perkId)
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

class RallyTestEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit RallyTestEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsRallyTest : public BattleTestFixture
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
		if(!setPerkStatus(perkRules, disciplineSkillId, rallyPerkId)
			|| !setPerkStatus(perkRules, luckSkillId, twistOfFateId))
			throw std::runtime_error("Missing Rally or Twist of Fate from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_MORALE_CHANCE, badMoraleChance(badMoraleChancePercent));
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

	void selectRally(CGHeroInstance * hero)
	{
		const int decodedDiscipline = SecondarySkill::decode(disciplineSkillId);
		ASSERT_GE(decodedDiscipline, 0);
		hero->setSecSkillLevel(SecondarySkill(decodedDiscipline), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, disciplineSkillId, rallyPerkId);
		ASSERT_TRUE(hero->hasActivePerk(disciplineSkillId, rallyPerkId));
	}

	void selectTwistOfFate(CGHeroInstance * hero)
	{
		const int decodedLuck = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decodedLuck, 0);
		const auto luck = SecondarySkill(decodedLuck);

		hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, fortuneFavorId);
		hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, chainOfFortuneId);
		hero->setSecSkillLevel(luck, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, luckSkillId, twistOfFateId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, twistOfFateId));
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void startRallyBattle(bool attackerHasRally, bool defenderHasRally, bool defenderHasTwist = false,
		int badChance = 100)
	{
		badMoraleChancePercent = badChance;
		startGame();
		if(attackerHasRally)
			selectRally(attackerSideHero);
		if(defenderHasRally)
			selectRally(defenderSideHero);
		if(defenderHasTwist)
			selectTwistOfFate(defenderSideHero);

		startBattle();
		EXPECT_EQ(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).enabled, attackerHasRally);
		EXPECT_EQ(battle()->getMoraleSuppressionState(BattleSide::DEFENDER).enabled, defenderHasRally);
		removeDeployedUnits();
	}

	std::optional<int> seedForFirstBadMorale(ObjectInstanceID army, int moraleMagnitude) const
	{
		for(int seed = 1; seed < 100000; ++seed)
		{
			GameRandomizer expected(*gameState());
			expected.setSeed(seed);
			if(expected.rollBadMorale(army, moraleMagnitude))
				return seed;
		}
		return std::nullopt;
	}

	void defendActiveStack()
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
	}

	void advanceUntilActive(const CStack * expected)
	{
		for(int attempt = 0; attempt < 16; ++attempt)
		{
			if(battle()->battleActiveUnit() == expected)
				return;
			ASSERT_NE(battle()->battleActiveUnit(), nullptr);
			defendActiveStack();
		}
		FAIL() << "The expected stack did not receive an ordinary activation";
	}

	CStack * addNegativeMoraleStack(BattleSide originalSide, const CreatureID & creature, int morale = -20)
	{
		auto * stack = addStack(originalSide, creature, originalSide == BattleSide::ATTACKER
			? BattleHex(leftHex) : BattleHex(rightHex), 10);
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MORALE, BonusSource::OTHER, morale, BonusSourceID()));
		return stack;
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

	int badMoraleChancePercent = 100;
};
}

TEST_F(NewHorizonsRallyTest, FirstNegativeMoraleIsCanceledForItsCurrentControllerAndLeavesTwistAvailable)
{
	startRallyBattle(false, true, true, 50);
	ASSERT_TRUE(defenderSideHero->hasActivePerk(disciplineSkillId, rallyPerkId));
	ASSERT_TRUE(defenderSideHero->hasActivePerk(luckSkillId, twistOfFateId));

	auto * hypnotized = addNegativeMoraleStack(BattleSide::ATTACKER, creatureByName("core:archangel"));
	hypnotized->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(hypnotized->unitSide(), BattleSide::ATTACKER);
	ASSERT_EQ(battle()->battleGetOwner(hypnotized), PlayerColor(1));
	ASSERT_EQ(battle()->playerToSide(battle()->battleGetOwner(hypnotized)), BattleSide::DEFENDER);
	ASSERT_LT(hypnotized->moraleVal(), 0);
	ASSERT_TRUE(gameHandler->randomizer->isBadMoraleRollStochastic(-hypnotized->moraleVal()));
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 20);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 20);
	const auto seed = seedForFirstBadMorale(battle()->getSideArmy(hypnotized->unitSide())->id,
		-hypnotized->moraleVal());
	ASSERT_TRUE(seed.has_value()) << "Could not find a deterministic first bad-Morale stream";

	gameHandler->randomizer->setSeed(*seed);
	beginCombat();
	advanceUntilActive(hypnotized);

	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::DEFENDER).used)
		<< "The controller of a hypnotized negative-Morale recipient spends Rally";
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available())
		<< "Rally cancels the failed trigger before Twist can consume its allowance";
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).enabled);
	EXPECT_EQ(activationCount(server.stackActivations, hypnotized->unitId(), BattleUnitTurnReason::TURN_QUEUE), 1u)
		<< "A canceled trigger reaches the ordinary turn queue instead of an automatic no-action activation";
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Rally cancels the first negative Morale trigger") != std::string::npos;
	})) << "The cancellation is reported in combat feedback";
}

TEST_F(NewHorizonsRallyTest, RallyIsOncePerCombatAndDoesNotResetAtTheNextRound)
{
	startRallyBattle(true, false);
	auto * afflicted = addNegativeMoraleStack(BattleSide::ATTACKER, creatureByName("core:archangel"));
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 20);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 20);
	beginCombat();
	advanceUntilActive(afflicted);

	ASSERT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).used);
	EXPECT_EQ(activationCount(server.stackActivations, afflicted->unitId(), BattleUnitTurnReason::TURN_QUEUE), 1u);
	const auto firstRound = battle()->getRound();
	defendActiveStack();
	endRound();
	for(int attempt = 0; attempt < 8
		&& activationCount(server.stackActivations, afflicted->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION) == 0;
		++attempt)
		defendActiveStack();

	EXPECT_GT(battle()->getRound(), firstRound);
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).used)
		<< "Rally is battle-long; crossing a round boundary cannot restore its consumed use";
	EXPECT_GE(activationCount(server.stackActivations, afflicted->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION), 1u)
		<< "The next guaranteed bad-Morale trigger proceeds after Rally has been spent";
}

TEST_F(NewHorizonsRallyTest, MoraleImmunityAndAnUnselectedPerkDoNotConsumeRally)
{
	startRallyBattle(true, false);
	auto * immune = addNegativeMoraleStack(BattleSide::ATTACKER, creatureByName("core:archangel"), -20);
	immune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_MORALE, BonusSource::OTHER, 0, BonusSourceID()));
	ASSERT_EQ(immune->moraleVal(), 0);
	auto * unselectedSide = addNegativeMoraleStack(BattleSide::DEFENDER, creatureByName("core:archangel"));
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 20);
	beginCombat();
	for(int attempt = 0; attempt < 4
		&& activationCount(server.stackActivations, unselectedSide->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION) == 0;
		++attempt)
		defendActiveStack();

	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).available())
		<< "A NO_MORALE stack has no negative-Morale trigger for Rally to consume";
	EXPECT_FALSE(battle()->getMoraleSuppressionState(BattleSide::DEFENDER).enabled);
	EXPECT_GE(activationCount(server.stackActivations, unselectedSide->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION), 1u)
		<< "A side without the perk retains the ordinary negative-Morale failure";
}

TEST_F(NewHorizonsRallyTest, StatePacketAndDetachedBranchesPreserveIndependentExpenditure)
{
	MoraleSuppressionState spent{true, true};
	CMemorySerializer stateWire;
	stateWire.oser & spent;
	MoraleSuppressionState restored;
	stateWire.iser & restored;
	EXPECT_EQ(restored, spent);

	CMemorySerializer rejectedDowngrade;
	rejectedDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_ADVERSE_COMBAT_REROLL;
	EXPECT_THROW(rejectedDowngrade.oser & spent, std::runtime_error);
	EXPECT_TRUE(rejectedDowngrade.extractBuffer().empty());

	CMemorySerializer legacyWire;
	legacyWire.oser.version = legacyWire.iser.version = ESerializationVersion::NEW_HORIZONS_ADVERSE_COMBAT_REROLL;
	MoraleSuppressionState legacyValue;
	legacyWire.oser & legacyValue;
	MoraleSuppressionState legacyRestored{true, true};
	legacyWire.iser & legacyRestored;
	EXPECT_EQ(legacyRestored, MoraleSuppressionState{});

	BattleMoraleSuppressionStateChanged packet;
	packet.battleID = BattleID(0);
	packet.side = BattleSide::ATTACKER;
	packet.state = spent;
	EXPECT_NO_THROW(packet.validateTransitionFrom({true, false}));
	EXPECT_NO_THROW(packet.validateTransitionFrom({true, true}));
	EXPECT_THROW(packet.validateTransitionFrom({}), std::runtime_error);
	CMemorySerializer packetWire;
	packetWire.oser & packet;
	BattleMoraleSuppressionStateChanged restoredPacket;
	packetWire.iser & restoredPacket;
	EXPECT_EQ(restoredPacket.side, BattleSide::ATTACKER);
	EXPECT_EQ(restoredPacket.state, spent);
	CMemorySerializer rejectedPacketDowngrade;
	rejectedPacketDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_ADVERSE_COMBAT_REROLL;
	EXPECT_THROW(rejectedPacketDowngrade.oser & packet, std::runtime_error);
	EXPECT_TRUE(rejectedPacketDowngrade.extractBuffer().empty());

	startRallyBattle(true, false);
	ASSERT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).available());

	auto * original = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex), 10);
	RallyTestEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto projected = branch->getForUpdate(original->unitId());
	ASSERT_NE(projected, nullptr);
	const auto firstDelta = branch->projectMoraleActivationDelta(original, projected.get(), -0.35f, -0.70f, 2.0f);
	EXPECT_FLOAT_EQ(firstDelta, -0.35f)
		<< "Rally protects one prospective negative-Morale event, leaving the later event exposed";
	EXPECT_TRUE(branch->getMoraleSuppressionState(BattleSide::ATTACKER).used);
	const auto secondDelta = branch->projectMoraleActivationDelta(original, projected.get(), -0.35f, -0.70f, 2.0f);
	EXPECT_FLOAT_EQ(secondDelta, -0.70f)
		<< "The same hypothetical branch cannot spend Rally a second time";
	EXPECT_TRUE(parent->getMoraleSuppressionState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(sibling->getMoraleSuppressionState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::ATTACKER).available());
}
