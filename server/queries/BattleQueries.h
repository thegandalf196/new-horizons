/*
 * BattleQueries.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CQuery.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/battle/BattleSide.h"

#include <functional>
#include <vector>

class IBattleInfo;
struct SideInBattle;

class CBattleQuery : public CQuery
{
public:
	static constexpr QueryType TYPE = QueryType::Battle;

	BattleSideArray<const CArmedInstance *> belligerents;

	BattleID battleID;
	std::optional<BattleResult> result;
	std::vector<ObjectInstanceID> heroesWithDeferredLevelUp;
	mutable bool deferredLevelUpsApplied = false;

	bool hasPendingBattleOrVisitQueries() const;
	std::vector<ObjectInstanceID> takeDeferredLevelUps();
	void completeDeferredLevelUps() const;

	CBattleQuery(CGameHandler * owner);
	CBattleQuery(CGameHandler * owner, const IBattleInfo * Bi);
	void notifyObjectAboutRemoval(const CGObjectInstance * visitedObject, const CGHeroInstance * visitingHero) const override;
	bool blocksPack(const CPackForServer *pack) const override;
	void onRemoval(PlayerColor color) override;
	void onExposure(QueryPtr topQuery) override;
};

class CBattleDialogQuery : public CDialogQuery
{
	bool resultProcessed = false;
	const IBattleInfo * bi;
	std::optional<BattleResult> result;

public:
	static constexpr QueryType TYPE = QueryType::BattleDialog;
	CBattleDialogQuery(CGameHandler * owner, const IBattleInfo * Bi, const std::optional<BattleResult> & Br);
	void onRemoval(PlayerColor color) override;
	void onExposure(QueryPtr topQuery) override;
};

/// Server-owned, index-validated choice used by post-battle Necromancy.
/// The visible prompt is a normal BlockingDialog packet so old clients can
/// still render it, while this query prevents arbitrary QueryReply integers.
class CNecromancyQuery : public CQuery
{
	std::vector<CreatureID> choices;
	std::function<void(std::optional<CreatureID>)> callback;
	std::optional<int32_t> answer;

public:
	static constexpr QueryType TYPE = QueryType::NecromancyChoice;

	CNecromancyQuery(CGameHandler * owner, PlayerColor player,
		std::vector<CreatureID> choices,
		std::function<void(std::optional<CreatureID>)> callback);

	bool blocksPack(const CPackForServer * pack) const override;
	bool endsByPlayerAnswer() const override;
	bool isValidReply(std::optional<int32_t> reply) const override;
	void setReply(std::optional<int32_t> reply) override;
	void onRemoval(PlayerColor color) override;
};
