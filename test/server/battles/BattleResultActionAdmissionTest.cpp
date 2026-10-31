/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/logging/CLogger.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/CRandomGenerator.h"

#include <array>
#include <mutex>
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

namespace
{
struct AdmissionErrors
{
	std::mutex mutex;
	std::vector<std::string> messages;
};
class AdmissionErrorTarget final : public ILogTarget
{
	std::weak_ptr<AdmissionErrors> errors;
public:
	explicit AdmissionErrorTarget(std::weak_ptr<AdmissionErrors> value) : errors(std::move(value)) {}
	void write(const LogRecord & record) override
	{
		if(record.domain.getName() != CLoggerDomain::DOMAIN_GLOBAL || record.level != ELogLevel::ERROR)
			return;
		if(const auto state = errors.lock())
		{
			std::lock_guard lock(state->mutex);
			state->messages.push_back(record.message);
		}
	}
};
class AdmissionErrorCapture final
{
	CLogger * logger = CLogger::getGlobalLogger();
	ELogLevel::ELogLevel previousLevel = logger->getLevel();
	std::shared_ptr<AdmissionErrors> errors = std::make_shared<AdmissionErrors>();
public:
	AdmissionErrorCapture()
	{
		logger->setLevel(ELogLevel::ERROR);
		logger->addTarget(std::make_unique<AdmissionErrorTarget>(errors));
	}
	~AdmissionErrorCapture() { logger->setLevel(previousLevel); }
	std::vector<std::string> messages() const
	{
		std::lock_guard lock(errors->mutex);
		return errors->messages;
	}
};
struct AdmissionState
{
	int round;
	int activeId;
	std::vector<std::pair<uint32_t, JsonNode>> units;
	std::vector<std::pair<uint32_t, uint32_t>> movement;
	std::array<int64_t, 2> mana;
	std::array<bool, 2> commandUsed;
	std::array<HeroCommand, 2> order;
	std::array<HeroCommand, 2> doctrine;
	std::vector<std::byte> serverRng;
	bool operator==(const AdmissionState &) const = default;
};
}

class BattleResultActionAdmissionTest : public HeroCommandFixture
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		// One human is required for the real accept/retry result query.
		if(settings.color == PlayerColor(1)) settings.connectedPlayerIDs.clear();
	}
	void prepare()
	{
		useRevisedOrders = true;
		prepareCommands(true);
	}
	void submit(const BattleAction & action, PlayerColor player = PlayerColor(0), BattleID id = BattleID(0))
	{
		MakeAction request(action);
		request.player = player;
		request.battleID = id;
		gameHandler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
	}
	AdmissionState admissionState(const BattleInfo & info)
	{
		AdmissionState value;
		value.round = info.getRound();
		value.activeId = info.getActiveStackID();
		for(const auto * unit : info.battleGetAllStacks())
		{
			// Reuse the unit-state codec on a detached copy, not whole-world serialization.
			value.units.emplace_back(unit->unitId(), unit->acquireState()->save());
			value.movement.emplace_back(unit->unitId(), unit->getMovementRange());
		}
		value.mana = {attackerSideHero->getManaAvailable(), defenderSideHero->getManaAvailable()};
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto index = static_cast<size_t>(side);
			value.commandUsed[index] = info.getHeroCommandUsed(side);
			value.order[index] = info.battleGetActiveOrder(side);
			value.doctrine[index] = info.battleGetActiveDoctrine(side);
		}
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		value.serverRng = wire.extractBuffer();
		return value;
	}
	void addExpirySentinel()
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		const auto speed = active->getMovementRange();
		Bonus sentinel;
		sentinel.type = BonusType::STACKS_SPEED;
		sentinel.val = 5;
		sentinel.duration = BonusDuration::STACK_GETS_TURN;
		SetStackEffect effect;
		effect.battleID = battle()->getBattleID();
		effect.toAdd.emplace_back(active->unitId(), std::vector<Bonus>{sentinel});
		gameHandler->sendAndApply(effect);
		ASSERT_GT(active->getMovementRange(), speed);
	}
	void expectLiveRefusal(const BattleAction & action, const std::string & expectedReason)
	{
		ASSERT_NO_FATAL_FAILURE(addExpirySentinel());
		const auto battleId = battle()->getBattleID();
		const auto * originalBattle = battle();
		const auto state = admissionState(*battle());
		const auto starts = server.startedActions.size();
		const auto activations = server.stackActivations.size();
		AdmissionErrorCapture errors;
		submit(action);
		ASSERT_EQ(gameState()->getBattle(battleId), originalBattle);
		ASSERT_NO_FATAL_FAILURE(sameRejectedActivation(starts, activations, static_cast<uint32_t>(state.activeId)));
		EXPECT_EQ(admissionState(*battle()), state);
		EXPECT_EQ(errors.messages(), std::vector<std::string>{expectedReason});
	}
	void sameRejectedActivation(size_t starts, size_t activations, uint32_t activeId)
	{
		EXPECT_EQ(server.startedActions.size(), starts);
		ASSERT_EQ(server.stackActivations.size(), activations + 1);
		EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::ACTION_REJECTED);
		EXPECT_EQ(server.stackActivations.back().stack, activeId);
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		EXPECT_EQ(battle()->battleActiveUnit()->unitId(), activeId);
	}
};
TEST_F(BattleResultActionAdmissionTest, StaleStackIdentityRejectsWhileBattleQueryStillAllowsRequests)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto id = active->unitId();
	const auto units = battle()->battleGetAllStacks();
	const auto other = std::find_if(units.begin(), units.end(), [id](const CStack * stack) { return stack->unitId() != id; });
	ASSERT_NE(other, units.end());
	auto action = BattleAction::makeDefend(active);
	action.stackNumber = (*other)->unitId();
	MakeAction request(action);
	request.player = PlayerColor(0);
	request.battleID = BattleID(0);
	ASSERT_FALSE(gameHandler->isBlockedByQueries(&request, request.player));
	ASSERT_NO_FATAL_FAILURE(expectLiveRefusal(action, "Can not make actions - stack is not active!"));
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}
TEST_F(BattleResultActionAdmissionTest, SharedBudgetRejectionDoesNotReserveOrAdvanceAnotherActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_NE(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	const auto mana = attackerSideHero->getManaAvailable();
	auto action = heroAction(0);
	ASSERT_NO_FATAL_FAILURE(expectLiveRefusal(action, "Hero spell unavailable under the shared round action budget"));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::CHARGE);
}
TEST_F(BattleResultActionAdmissionTest, SecondWaitRejectsAfterTheSameUnitNaturallyReturnsInItsRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * first = battle()->battleActiveUnit();
	ASSERT_NE(first, nullptr);
	const auto id = first->unitId();
	const auto round = battle()->getRound();
	submit(BattleAction::makeWait(first));
	for(int steps = 0; steps < 16 && battle()->battleActiveUnit() && battle()->battleActiveUnit()->unitId() != id; ++steps)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		submit(BattleAction::makeDefend(active), battle()->battleGetActionController(active));
	}
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->getRound(), round);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), id);
	ASSERT_TRUE(battle()->getStack(id)->waitedThisTurn);
	ASSERT_NO_FATAL_FAILURE(expectLiveRefusal(BattleAction::makeWait(battle()->battleActiveUnit()),
		"This stack has already waited this round!"));
	EXPECT_TRUE(battle()->getStack(id)->waitedThisTurn);
}
class BattleResultReplyAdmissionTest : public BattleResultActionAdmissionTest,
	public ::testing::WithParamInterface<int>
{
protected:
	void prepareRealResultBattle()
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_TRUE(gameState()->getStartInfo()->playerInfos.at(PlayerColor(0)).isControlledByHuman());
		ASSERT_FALSE(gameState()->getStartInfo()->playerInfos.at(PlayerColor(1)).isControlledByHuman());
		ASSERT_TRUE(gameState()->players.at(PlayerColor(0)).isHuman());
		ASSERT_FALSE(gameState()->players.at(PlayerColor(1)).isHuman());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		// The arithmetic fixture's startBattle sends only BattleStart. Result
		// tests need real startup, which installs the authoritative CBattleQuery.
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		for(int phase = 0; phase < 2; ++phase)
		{
			const auto & deployment = battle()->getDeploymentState();
			const auto side = deployment.independent ? deployment.activeSide()
				: (battle()->tacticDistance > 0 ? battle()->tacticsSide : BattleSide::NONE);
			if(side == BattleSide::NONE) break;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),
				battle()->sideToPlayer(side), BattleAction::makeEndOFTacticPhase(side)));
		}
		ASSERT_EQ(battle()->tacticDistance, 0);
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
		const auto query = std::dynamic_pointer_cast<CBattleQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
		ASSERT_NE(query, nullptr);
		ASSERT_EQ(query->battleID, battle()->getBattleID());
		ASSERT_TRUE(battle()->replayAllowed);
	}
};
TEST_P(BattleResultReplyAdmissionTest, PendingResultBlocksLateActionsBeforeBattleDispatchAndReplyCompletes)
{
	ASSERT_NO_FATAL_FAILURE(prepareRealResultBattle());
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto lateDefend = BattleAction::makeDefend(active);
	const auto lateWait = BattleAction::makeWait(active);
	const auto lateSpell = heroAction(0);
	const auto oldId = battle()->getBattleID();
	// Controlled server completion exercises the real result/query lifecycle;
	// this is not a replay of the reported Ivor/Halfling combat or its AI loop.
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	const auto dialog = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(dialog, nullptr);
	ASSERT_EQ(dialog->getType(), QueryType::BattleDialog);
	ASSERT_NE(gameState()->getBattle(oldId), nullptr);
	const auto starts = server.startedActions.size();
	const auto activations = server.stackActivations.size();
	const auto resultState = admissionState(*gameState()->getBattle(oldId));
	for(const auto & action : {lateDefend, lateWait, lateSpell})
	{
		MakeAction request(action);
		request.player = PlayerColor(0);
		request.battleID = oldId;
		ASSERT_TRUE(gameHandler->isBlockedByQueries(&request, request.player));
		AdmissionErrorCapture errors;
		gameHandler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
		EXPECT_EQ(server.startedActions.size(), starts);
		EXPECT_EQ(server.stackActivations.size(), activations);
		EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), dialog);
		ASSERT_NE(gameState()->getBattle(oldId), nullptr);
		EXPECT_EQ(admissionState(*gameState()->getBattle(oldId)), resultState);
		const auto messages = errors.messages();
		ASSERT_EQ(messages.size(), 1u);
		EXPECT_NE(messages.front().find("has to answer queries before attempting any further actions."), std::string::npos);
		EXPECT_NE(messages.front().find(dialog->toString()), std::string::npos);
	}
	QueryReply reply(dialog->queryID, GetParam());
	reply.player = PlayerColor(0);
	ASSERT_FALSE(gameHandler->isBlockedByQueries(&reply, reply.player));
	gameHandler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, reply);
	ASSERT_EQ(gameState()->getBattle(oldId), nullptr);
	if(GetParam() == 0)
		EXPECT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	else
	{
		const auto * restarted = gameState()->getBattle(PlayerColor(0));
		ASSERT_NE(restarted, nullptr);
		ASSERT_NE(restarted->getBattleID(), oldId);
		const auto newId = restarted->getBattleID();
		const auto afterStarts = server.startedActions.size();
		const auto afterActivations = server.stackActivations.size();
		const auto restartedState = admissionState(*restarted);
		const auto restartedQuery = gameHandler->queries->topQuery(PlayerColor(0));
		MakeAction obsolete(lateDefend);
		obsolete.player = PlayerColor(0);
		obsolete.battleID = oldId;
		ASSERT_FALSE(gameHandler->isBlockedByQueries(&obsolete, obsolete.player));
		AdmissionErrorCapture errors;
		gameHandler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, obsolete);
		EXPECT_EQ(server.startedActions.size(), afterStarts);
		EXPECT_EQ(server.stackActivations.size(), afterActivations);
		ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
		EXPECT_EQ(gameState()->getBattle(PlayerColor(0))->getBattleID(), newId);
		EXPECT_EQ(admissionState(*gameState()->getBattle(PlayerColor(0))), restartedState);
		EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), restartedQuery);
		EXPECT_TRUE(errors.messages().empty()); // Missing BattleID is a silent authoritative refusal.
	}
}
INSTANTIATE_TEST_SUITE_P(AcceptAndRetry, BattleResultReplyAdmissionTest, ::testing::Values(0, 1));
