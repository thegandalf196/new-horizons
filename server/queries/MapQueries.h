/*
 * MapQueries.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CQuery.h"
#include "../../lib/networkPacks/PacksForClient.h"

class CGHeroInstance;
class CGObjectInstance;
class IObjectInterface;
class CArmedInstance;

//Created when player starts turn or when player puts game on [ause
//Removed when player accepts a turn or continur play
class TimerPauseQuery : public CQuery
{
public:
	static constexpr QueryType TYPE = QueryType::TimerPause;

	TimerPauseQuery(CGameHandler * owner, PlayerColor player);

	bool blocksPack(const CPackForServer *pack) const override;
	void onExposure(QueryPtr topQuery) override;
	void onAdding(PlayerColor color) override;
	void onRemoval(PlayerColor color) override;
	bool endsByPlayerAnswer() const override;
};

//Created when hero attempts move and something happens
//(not necessarily position change, could be just an object interaction).
class CHeroMovementQuery : public CQuery
{
public:
	static constexpr QueryType TYPE = QueryType::HeroMovement;

	TryMoveHero tmh;
	bool visitDestAfterVictory; //if hero moved to guarded tile and it should be visited once guard is defeated
	const CGHeroInstance *hero;

	void onExposure(QueryPtr topQuery) override;

	CHeroMovementQuery(CGameHandler * owner, const TryMoveHero & Tmh, const CGHeroInstance * Hero, bool VisitDestAfterVictory = false);
	void onAdding(PlayerColor color) override;
	void onRemoval(PlayerColor color) override;
};

class CGarrisonDialogQuery : public CDialogQuery //used also for hero exchange dialogs
{
public:
	static constexpr QueryType TYPE = QueryType::GarrisonDialog;

	std::array<const CArmedInstance *,2> exchangingArmies;

	CGarrisonDialogQuery(CGameHandler * owner, const CArmedInstance *up, const CArmedInstance *down);
	void notifyObjectAboutRemoval(const CGObjectInstance * visitedObject, const CGHeroInstance * visitingHero) const override;
	bool blocksPack(const CPackForServer *pack) const override;
};

//yes/no and component selection dialogs
class CBlockingDialogQuery : public CDialogQuery
{
public:
	static constexpr QueryType TYPE = QueryType::BlockingDialog;

	const IObjectInterface * caller;
	BlockingDialog bd; //copy of pack... debug purposes

	CBlockingDialogQuery(CGameHandler * owner, const IObjectInterface * caller, const BlockingDialog &bd);

	void notifyObjectAboutRemoval(const CGObjectInstance * visitedObject, const CGHeroInstance * visitingHero) const override;
};

class OpenWindowQuery : public CDialogQuery
{
	EOpenWindowMode mode;
public:
	static constexpr QueryType TYPE = QueryType::OpenWindow;

	OpenWindowQuery(CGameHandler * owner, const CGHeroInstance *hero, EOpenWindowMode mode);

	bool blocksPack(const CPackForServer *pack) const override;
	void onExposure(QueryPtr topQuery) override;
};

class CTeleportDialogQuery : public CDialogQuery
{
public:
	static constexpr QueryType TYPE = QueryType::TeleportDialog;

	TeleportDialog td; //copy of pack... debug purposes

	CTeleportDialogQuery(CGameHandler * owner, const TeleportDialog & dialog);

	void notifyObjectAboutRemoval(const CGObjectInstance * visitedObject, const CGHeroInstance * visitingHero) const override;
};

class CHeroLevelUpDialogQuery : public CDialogQuery
{
private:
	void refreshChoicesBeforePrompt();
	bool canReroll() const;
	bool hasValidOffer() const;
	bool confirmCurrentOffer(PlayerColor player, QueryID requestedID) const;
	const newHorizonsHeroes::PerkState::OfferWeightContext & perkWeightContext() const;
	mutable newHorizonsHeroes::PerkState::OfferWeightContext cachedPerkWeightContext;
	mutable JsonNode cachedPerkNormalization;

public:
	static constexpr QueryType TYPE = QueryType::HeroLevelUpDialog;

	CHeroLevelUpDialogQuery(CGameHandler * owner, const HeroLevelUp &Hlu, const CGHeroInstance * Hero);
	// QueryReply negative values encode exactly one secondary-skill card.
	static constexpr int32_t REROLL_REPLY = -2;
	static constexpr int32_t rerollReply(size_t skillIndex) { return -2 - static_cast<int32_t>(skillIndex); }
	bool requestReroll(PlayerColor player, size_t skillIndex = 0);
	std::set<SecondarySkill> rerollCandidates() const;
	bool hasOptionalPolicy() const;

	void onRemoval(PlayerColor color) override;
	bool isValidReply(std::optional<int32_t> reply) const override;
	void onAdded(PlayerColor color) override;
	void onExposure(QueryPtr topQuery) override;
	void notifyObjectAboutRemoval(const CGObjectInstance * visitedObject, const CGHeroInstance * visitingHero) const override;

	HeroLevelUp hlu;
	const CGHeroInstance * hero;
	bool prompted = false;
};

class CHeroMasteryDialogQuery : public CQuery
{
public:
	static constexpr QueryType TYPE = QueryType::HeroMasteryDialog;
	CHeroMasteryDialogQuery(CGameHandler * owner, const CGHeroInstance * hero);
	bool blocksPack(const CPackForServer * pack) const override;
	void onAdded(PlayerColor color) override;
	void onExposure(QueryPtr topQuery) override;
	void onRemoval(PlayerColor color) override;
	void notifyObjectAboutRemoval(const CGObjectInstance * object, const CGHeroInstance * hero) const override;
	ObjectInstanceID heroId;
	bool accepted = false;
private:
	bool prompted = false;
};

class CommandEnhancementQuery : public CQuery
{
	bool prompted = false;
public:
	static constexpr QueryType TYPE = QueryType::HeroCommandEnhancement;
	CommandEnhancementQuery(CGameHandler * owner, newHorizonsHeroes::CommandEnhancementOffer offer);
	const newHorizonsHeroes::CommandEnhancementOffer offer;
	bool accepted = false;
	bool wasPrompted() const { return prompted; }
	bool endsByPlayerAnswer() const override { return false; }
	bool blocksPack(const CPackForServer * pack) const override;
	void onAdded(PlayerColor color) override;
	void onExposure(QueryPtr topQuery) override;
	void onRemoval(PlayerColor color) override;
	void notifyObjectAboutRemoval(const CGObjectInstance * object, const CGHeroInstance * hero) const override;
};

class CCommanderLevelUpDialogQuery : public CDialogQuery
{
public:
	static constexpr QueryType TYPE = QueryType::CommanderLevelUpDialog;

	CCommanderLevelUpDialogQuery(CGameHandler * owner, const CommanderLevelUp &Clu, const CGHeroInstance * Hero);

	void onRemoval(PlayerColor color) override;
	void onExposure(QueryPtr topQuery) override;
	void onAdded(PlayerColor color) override;
	void notifyObjectAboutRemoval(const CGObjectInstance * visitedObject, const CGHeroInstance * visitingHero) const override;

	CommanderLevelUp clu;
	const CGHeroInstance * hero;
	bool prompted = false;
};
