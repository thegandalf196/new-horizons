/*
 * NewHorizonsAdverseCombatRerollTest.cpp, part of VCMI engine
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
#include "../../../lib/battle/AdverseCombatRerollState.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../include/vcmi/ServerCallback.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace
{
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorId = "new-horizons:luck.fortuneSFavor";
constexpr auto chainOfFortuneId = "new-horizons:luck.chainOfFortune";
constexpr auto twistOfFateId = "new-horizons:luck.twistOfFate";

bool setTwistOfFateStatus(JsonNode & rules, std::string_view status)
{
	auto & perks = rules["skills"][luckSkillId]["perks"].Vector();
	const auto twist = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == twistOfFateId;
	});
	if(twist == perks.end())
		return false;
	(*twist)["effect"]["status"].String() = std::string(status);
	return true;
}

class AdverseCombatRerollEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit AdverseCombatRerollEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsAdverseCombatRerollTest : public BattleTestFixture
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
		if(!setTwistOfFateStatus(perkRules, "active"))
			throw std::runtime_error("Missing Twist of Fate from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
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

	void selectTwistOfFate(CGHeroInstance * hero)
	{
		const int decodedLuck = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decodedLuck, 0);
		const auto luck = SecondarySkill(decodedLuck);

		// Follow the real rank prerequisites and offer selection. Twist of Fate is
		// globally planned, so mapLoaded activates it only in this fixture's rules.
		hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, fortuneFavorId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, fortuneFavorId));
		hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, chainOfFortuneId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, chainOfFortuneId));
		hero->setSecSkillLevel(luck, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, twistOfFateId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, twistOfFateId));
	}

	void startTwistBattle(bool bothSides = false)
	{
		startGame();
		selectTwistOfFate(attackerSideHero);
		if(bothSides)
			selectTwistOfFate(defenderSideHero);
		startBattle();
		ASSERT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).enabled);
		EXPECT_EQ(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).enabled, bothSides);
	}

	bool resolve(BattleSide side, bool stochastic, bool adverseOnTrue, const std::function<bool()> & draw)
	{
		return gameHandler->battles->resolveAdverseCombatRoll(BattleID(0), side,
			stochastic, adverseOnTrue, draw);
	}
};
}

TEST(AdverseCombatRerollStateRulesTest, StateAndPacketSerializationValidateLegacyAndMonotonicTransitions)
{
	AdverseCombatRerollState state;
	state.enabled = true;
	state.used = true;
	CMemorySerializer stateWire;
	stateWire.oser & state;
	AdverseCombatRerollState restored;
	stateWire.iser & restored;
	EXPECT_EQ(restored, state);

	BattleAdverseRerollStateChanged packet;
	packet.battleID = BattleID(0);
	packet.side = BattleSide::ATTACKER;
	packet.state = state;
	EXPECT_NO_THROW(packet.validateTransitionFrom({true, false}));
	EXPECT_NO_THROW(packet.validateTransitionFrom({true, true}))
		<< "Replaying an already spent snapshot is idempotent";
	EXPECT_THROW(packet.validateTransitionFrom({}), std::runtime_error)
		<< "A state update cannot enable a perk that the battle snapshot did not enable";
	EXPECT_THROW(packet.validateTransitionFrom({false, false}), std::runtime_error);

	CMemorySerializer packetWire;
	packetWire.oser & packet;
	BattleAdverseRerollStateChanged restoredPacket;
	packetWire.iser & restoredPacket;
	EXPECT_EQ(restoredPacket.battleID, packet.battleID);
	EXPECT_EQ(restoredPacket.side, BattleSide::ATTACKER);
	EXPECT_EQ(restoredPacket.state, state);

	CMemorySerializer rejectedStateDowngrade;
	rejectedStateDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_CHAIN_OF_FORTUNE;
	EXPECT_THROW(rejectedStateDowngrade.oser & state, std::runtime_error);
	EXPECT_TRUE(rejectedStateDowngrade.extractBuffer().empty());

	CMemorySerializer rejectedPacketDowngrade;
	rejectedPacketDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_CHAIN_OF_FORTUNE;
	EXPECT_THROW(rejectedPacketDowngrade.oser & packet, std::runtime_error);
	EXPECT_TRUE(rejectedPacketDowngrade.extractBuffer().empty());

	CMemorySerializer legacyWire;
	legacyWire.oser.version = legacyWire.iser.version = ESerializationVersion::NEW_HORIZONS_CHAIN_OF_FORTUNE;
	AdverseCombatRerollState legacyInert;
	legacyWire.oser & legacyInert;
	AdverseCombatRerollState legacyRestored{true, false};
	legacyWire.iser & legacyRestored;
	EXPECT_EQ(legacyRestored, AdverseCombatRerollState{});

	CMemorySerializer rejectedLegacyPacket;
	rejectedLegacyPacket.iser.version = ESerializationVersion::NEW_HORIZONS_CHAIN_OF_FORTUNE;
	BattleAdverseRerollStateChanged ignoredPacket;
	EXPECT_THROW(rejectedLegacyPacket.iser & ignoredPacket, std::runtime_error);

	packet.side = BattleSide::NONE;
	EXPECT_THROW(packet.validateShape(), std::runtime_error);
	packet.side = BattleSide::ATTACKER;
	packet.state = {false, true};
	EXPECT_THROW(packet.validateShape(), std::runtime_error);

	AdverseCombatRerollState allowance{true, false};
	EXPECT_FALSE(allowance.consume(false, true));
	EXPECT_FALSE(allowance.consume(true, false));
	EXPECT_TRUE(allowance.available());
	EXPECT_TRUE(allowance.consume(true, true));
	EXPECT_FALSE(allowance.available());
	EXPECT_FALSE(allowance.consume(true, true));
}

TEST_F(NewHorizonsAdverseCombatRerollTest, DeterministicAndFavorableResultsDoNotSpendTheAllowance)
{
	startTwistBattle();
	int deterministicDraws = 0;
	EXPECT_TRUE(resolve(BattleSide::ATTACKER, false, true, [&deterministicDraws]
	{
		++deterministicDraws;
		return true;
	}));
	EXPECT_EQ(deterministicDraws, 1);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());

	int favorableDraws = 0;
	EXPECT_FALSE(gameHandler->spellcastEnvironment()->resolveAdverseCombatRoll(
		BattleID(0), BattleSide::ATTACKER, true, true, [&favorableDraws]
	{
		++favorableDraws;
		return false;
	}));
	EXPECT_EQ(favorableDraws, 1);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
}

TEST_F(NewHorizonsAdverseCombatRerollTest, FirstAdverseResultGetsOneFinalRedrawEvenWhenItIsAlsoAdverse)
{
	startTwistBattle();
	const auto logsBefore = server.battleLogLines.size();
	int draws = 0;
	const bool finalResult = resolve(BattleSide::ATTACKER, true, true, [this, &draws, logsBefore]
	{
		++draws;
		if(draws == 2)
		{
			EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used)
				<< "The spent state must be applied before the final draw";
			EXPECT_GT(server.battleLogLines.size(), logsBefore)
				<< "Reroll feedback must be published before the final draw";
		}
		return true;
	});
	EXPECT_TRUE(finalResult) << "The final result is not rerolled even if it remains adverse";
	EXPECT_EQ(draws, 2);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used);

	int laterDraws = 0;
	EXPECT_TRUE(resolve(BattleSide::ATTACKER, true, true, [&laterDraws]
	{
		++laterDraws;
		return true;
	}));
	EXPECT_EQ(laterDraws, 1);
}

TEST_F(NewHorizonsAdverseCombatRerollTest, SideAllowanceIsIndependentAndPersistsAcrossRounds)
{
	startTwistBattle(true);
	int attackerDraws = 0;
	EXPECT_FALSE(resolve(BattleSide::ATTACKER, true, true, [&attackerDraws]
	{
		++attackerDraws;
		return attackerDraws == 1;
	}));
	EXPECT_EQ(attackerDraws, 2);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());

	battle()->nextRound();
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());

	int defenderDraws = 0;
	EXPECT_FALSE(resolve(BattleSide::DEFENDER, true, true, [&defenderDraws]
	{
		++defenderDraws;
		return defenderDraws == 1;
	}));
	EXPECT_EQ(defenderDraws, 2);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).used);
}

TEST_F(NewHorizonsAdverseCombatRerollTest, NestedAdverseCallbackCannotConsumeTheAllowanceTwice)
{
	startTwistBattle();
	int outerDraws = 0;
	int nestedDraws = 0;
	bool nestedSawSpentState = false;
	bool nestedResult = false;
	const bool outerResult = resolve(BattleSide::ATTACKER, true, true, [this, &outerDraws,
		&nestedDraws, &nestedSawSpentState, &nestedResult]
	{
		++outerDraws;
		if(outerDraws == 1)
			return true;

		nestedSawSpentState = battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used;
		nestedResult = resolve(BattleSide::ATTACKER, true, true, [&nestedDraws]
		{
			++nestedDraws;
			return true;
		});
		return false;
	});

	EXPECT_FALSE(outerResult);
	EXPECT_EQ(outerDraws, 2);
	EXPECT_TRUE(nestedSawSpentState);
	EXPECT_TRUE(nestedResult);
	EXPECT_EQ(nestedDraws, 1) << "A nested adverse result sees the published expenditure";
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used);
}

TEST_F(NewHorizonsAdverseCombatRerollTest, HypotheticalBranchesCopyAndSpendStateWithoutChangingTheirParent)
{
	startTwistBattle();
	const auto liveState = battle()->getAdverseCombatRerollState(BattleSide::ATTACKER);
	ASSERT_TRUE(liveState.enabled);
	ASSERT_FALSE(liveState.used);

	auto environment = std::make_shared<AdverseCombatRerollEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto root = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), root);
	EXPECT_EQ(root->getAdverseCombatRerollState(BattleSide::ATTACKER), liveState);
	EXPECT_EQ(branch->getAdverseCombatRerollState(BattleSide::ATTACKER), liveState);

	BattleAdverseRerollStateChanged packet;
	packet.battleID = BattleID(0);
	packet.side = BattleSide::ATTACKER;
	packet.state = {true, true};
	EXPECT_EQ(branch->getBattleID(), packet.battleID);
	BattleStatePackVisitor visitor(*branch);
	packet.visitTyped(visitor);

	HypotheticBattle nested(environment.get(), branch);
	EXPECT_EQ(nested.getAdverseCombatRerollState(BattleSide::ATTACKER), packet.state)
		<< "A nested projection inherits the branch-local packet snapshot";
	EXPECT_TRUE(branch->getAdverseCombatRerollState(BattleSide::ATTACKER).used);
	EXPECT_EQ(root->getAdverseCombatRerollState(BattleSide::ATTACKER), liveState);
	EXPECT_EQ(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER), liveState);
}
