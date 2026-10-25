/* RouteActorObservationTest.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "mock/GameHandlerTestServer.h"
#include "AI/Nullkiller2/Helpers/RouteActorObservation.h"
#include "lib/callback/CCallback.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include <thread>

namespace
{
const PlayerColor OWNER(0);

template<class T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

class ActorRemovalServer final : public GameHandlerTestServer
{
public:
	std::shared_ptr<CCallback> callback;
	NK2AI::RouteActorObservation observation;
	bool notifyRemoval = true;
	std::vector<bool> accepted;

	explicit ActorRemovalServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), OWNER) {}
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * removal = dynamic_cast<const RemoveObject *>(&pack); removal && notifyRemoval)
		{
			if(const auto * hero = callback->getHero(removal->objectID))
				observation.objectRemoved(*hero);
		}
		GameHandlerTestServer::applyPack(pack);
	}
	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(const auto * applied = dynamic_cast<const PackageApplied *>(&pack))
			accepted.push_back(applied->result);
	}
};

class RouteActorObservationTest : public NullkillerTest
{
protected:
	CGHeroInstance * actor = nullptr;
	CGHeroInstance * survivor = nullptr;
	std::shared_ptr<CCallback> callback;
	std::unique_ptr<ActorRemovalServer> server;
	std::unique_ptr<CGameHandler> handler;

	void configurePlayer(PlayerSettings & settings) const override
	{
		settings.bonus = PlayerStartingBonus::GOLD;
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(OWNER).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), OWNER).heroGarrison({{CreatureID(0), 1}})
			.hero({10, 5, 0}, HeroTypeID(1), OWNER).heroGarrison({{CreatureID(0), 1}})
			.hero({30, 30, 0}, HeroTypeID(2), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		actor = findHeroAt({5, 5, 0});
		survivor = findHeroAt({10, 5, 0});
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(survivor, nullptr);
		revealMap(OWNER);
		callback = makeCallback(OWNER);
		gameState()->actingPlayers.insert(OWNER);
		server = std::make_unique<ActorRemovalServer>(gameState());
		server->callback = callback;
		handler = std::make_unique<CGameHandler>(*server, gameState());
	}
	void dismiss(ObjectInstanceID id)
	{
		DismissHero request(id);
		request.player = OWNER;
		request.requestID = 1;
		handler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
		ASSERT_EQ(server->accepted, std::vector<bool>({true}));
		ASSERT_EQ(callback->getHero(id), nullptr);
	}
};
}

TEST_F(RouteActorObservationTest, ActualAuthoritativeRemovalMatchesOnlyTheIssuedActor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = actor->id;
	const auto name = actor->getNameTextID();
	auto command = server->observation.arm(*actor, OWNER, *callback);
	{
		// The callback owns the authoritative mutation; read state only after joining.
		std::jthread authority([&]() { dismiss(id); });
	}
	ASSERT_EQ(callback->getHero(id), nullptr);
	const auto completed = gameState()->saveToMemory();
	const auto rng = serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto result = command->complete(*callback);
	ASSERT_TRUE(result);
	EXPECT_TRUE(result->observedRemoval);
	EXPECT_EQ(result->heroID, id);
	EXPECT_EQ(result->heroName, name);
	EXPECT_EQ(callback->getHero(survivor->id), survivor);
	EXPECT_EQ(gameState()->saveToMemory(), completed);
	EXPECT_EQ(serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), rng);
}

TEST_F(RouteActorObservationTest, MissingAuthoritativeNotificationIsNotCertifiedRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	server->notifyRemoval = false;
	const auto id = actor->id;
	auto command = server->observation.arm(*actor, OWNER, *callback);
	ASSERT_NO_FATAL_FAILURE(dismiss(id));
	const auto result = command->complete(*callback);
	ASSERT_TRUE(result);
	EXPECT_FALSE(result->observedRemoval);
	EXPECT_EQ(result->heroID, id);
}

TEST_F(RouteActorObservationTest, UnrelatedActorRemovalDoesNotInterruptLiveActor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto command = server->observation.arm(*actor, OWNER, *callback);
	ASSERT_NO_FATAL_FAILURE(dismiss(survivor->id));
	EXPECT_FALSE(command->complete(*callback));
	EXPECT_EQ(callback->getHero(actor->id), actor);
}

TEST_F(RouteActorObservationTest, ScopeUnwindDoesNotLeakRemovalReceiptToLaterCommand)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	{
		auto abandoned = server->observation.arm(*actor, OWNER, *callback);
	}
	ASSERT_NO_FATAL_FAILURE(dismiss(actor->id));
	auto next = server->observation.arm(*survivor, OWNER, *callback);
	EXPECT_FALSE(next->complete(*callback));
}

TEST_F(RouteActorObservationTest, PreexistingMissingOrWrongOwnerCannotArm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(dismiss(actor->id));
	EXPECT_THROW(server->observation.arm(*actor, OWNER, *callback), NK2AI::RouteActorUnavailable);
	EXPECT_THROW(server->observation.arm(*survivor, PlayerColor(1), *callback), NK2AI::RouteActorUnavailable);
}

TEST_F(RouteActorObservationTest, PrematureOrWrongCallbackCompletionIsExceptional)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto command = server->observation.arm(*actor, OWNER, *callback);
	server->observation.objectRemoved(*actor); // Pre-apply event is not enough while actor remains present.
	const auto premature = command->complete(*callback);
	ASSERT_TRUE(premature);
	EXPECT_FALSE(premature->observedRemoval);
	auto next = server->observation.arm(*survivor, OWNER, *callback);
	const auto wrongOwner = makeCallback(PlayerColor(1));
	const auto result = next->complete(*wrongOwner);
	ASSERT_TRUE(result);
	EXPECT_FALSE(result->observedRemoval);
}
