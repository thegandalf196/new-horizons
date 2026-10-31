/*
 * CGameStateMemory.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CGameState.h"
#include "CGameStateCampaign.h"
#include "TavernHeroesPool.h"
#include "QuestInfo.h"
#include "../StartInfo.h"
#include "../ScopeGuard.h"
#include "../CPlayerState.h"
#include "../GameSettings.h"
#include "../battle/BattleInfo.h"
#include "../bonuses/NativeBonusGraphAccess.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/Limiters.h"
#include "../bonuses/Propagators.h"
#include "../bonuses/Updaters.h"
#include "../campaign/CampaignState.h"
#include "../entities/artifact/CArtifactInstance.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
#include "../mapObjects/MiscObjects.h"
#include "../mapping/CMap.h"
#include "../rmg/CMapGenOptions.h"
#include "../serializer/CMemorySerializer.h"

std::vector<std::byte> CGameState::saveToMemory()
{
	return saveReplaySnapshot().bytes;
}

ReplayPayload CGameState::saveReplaySnapshot()
{
	NativeBonusGraphAccess graphAccess;
	// a snapshot is stored inside the replay log itself, so the log must not be part of it.
	// Swapping keeps the serialized layout identical - an empty log is written instead.
	ReplayLog logBackup;
	std::swap(logBackup, replayLog);
	const auto restoreLog = vstd::makeScopeGuard([this, &logBackup]() { std::swap(logBackup, replayLog); });

	CMemorySerializer serializer;
	serializer.oser & *this;

	return {serializer.extractBuffer(), serializer.oser.version};
}

void CGameState::loadFromMemory(std::vector<std::byte> data)
{
	loadReplaySnapshot(ReplayPayload{std::move(data), ESerializationVersion::CURRENT});
}

void CGameState::loadReplaySnapshot(ReplayPayload payload)
{
	replayPayload::requireKnownVersion(payload.version);
	auto rootMutation = battleRootLifetime.mutate();
	// battles are not part of serialize(), so leftovers of the discarded state have to go explicitly
	currentBattles.clear();

	CMemorySerializer serializer(std::move(payload.bytes));
	serializer.iser.version = payload.version;
	serializer.iser.loadingGamestate = true;
	serializer.iser.cb = this;
	serializer.iser & *this;
}

void CGameState::updateOnLoad(const StartInfo & si)
{
	assert(services);
	scenarioOps->playerInfos = si.playerInfos;
	for(auto & i : si.playerInfos)
	{
		// a random-map save stores the original (unpruned) StartInfo, which may list players absent from the actual game state
		if(!players.count(i.first))
			continue;
		players.at(i.first).human = i.second.isControlledByHuman();
		logGlobal->debug("Player %d is controlled by %s, team %d", i.first.getNum(), i.second.isControlledByHuman() ? "human" : "AI", players.at(i.first).team.getNum());
	}
	// pre-TOWN_NAME_TEXT_ID saves carry renames as free-form text; move them into the map container
	for(auto * town : getMap().getObjects<CGTownInstance>())
		if(!town->legacyCustomName.empty())
		{
			town->setCustomName(getMap(), town->legacyCustomName);
			town->legacyCustomName.clear();
		}

	const auto capturedNeutralRequest = scenarioOps->extraOptionsInfo.neutralArmySizePercent;
	scenarioOps->extraOptionsInfo = si.extraOptionsInfo;
	scenarioOps->extraOptionsInfo.neutralArmySizePercent = capturedNeutralRequest;
	scenarioOps->turnTimerInfo = si.turnTimerInfo;
	scenarioOps->simturnsInfo = si.simturnsInfo;
}
