/* Part of VCMI; GPL v2 or later, see license.txt. */
#pragma once
#include "../../../lib/callback/IClient.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/callback/CGlobalAI.h"
#include "../../../lib/callback/AIFactory.h"
#include "../../../lib/serializer/CTypeList.h"
#include "../../../server/ServerNetPackVisitors.h"
#include <future>
#include <chrono>

namespace
{
class LogisticsMasteryEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit LogisticsMasteryEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class LogisticsMasteryLoopback final : public IClient
{
	CGameHandler & handler;
public:
	std::weak_ptr<CGlobalAI> ai;
	std::promise<std::pair<int, bool>> completed;
	explicit LogisticsMasteryLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &, const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}
	int sendRequest(const CPackForServer & outgoing, PlayerColor player, bool waitTillRealize) override
	{
		constexpr int request = 17;
		const auto * reply = dynamic_cast<const HeroMasteryReply *>(&outgoing);
		if(!reply || reply->player != player || !waitTillRealize)
			throw std::runtime_error("Unexpected Logistics AI request");
		HeroMasteryReply pack = *reply;
		pack.requestID = request;
		const auto controller = ai.lock();
		if(!controller) throw std::runtime_error("Missing Logistics AI controller");
		controller->requestSent(&pack, request);
		ApplyGhNetPackVisitor visitor(handler, GameConnectionID(0));
		pack.visitTyped(visitor);
		PackageApplied ack;
		ack.player = player;
		ack.requestID = request;
		ack.packType = CTypeList::getInstance().getTypeID<HeroMasteryReply>(nullptr);
		ack.result = visitor.getResult();
		controller->requestRealized(&ack);
		completed.set_value({pack.choice, visitor.getResult()});
		return request;
	}
};
}
