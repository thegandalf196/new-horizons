/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../lib/StdInc.h"
#include "SaveParityEvidence.h"
#include "SaveParityInput.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/TurnTimerHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/processors/HeroPoolProcessor.h"
#include "../../server/processors/PlayerMessageProcessor.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include <algorithm>
#include <ctime>

namespace saveParity
{
namespace
{
template<typename T> std::vector<std::byte> bytes(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

class NormalizeTimestamps
{
	StatisticDataSet & statistics;
	std::vector<time_t> original;
public:
	explicit NormalizeTimestamps(StatisticDataSet & value) : statistics(value)
	{
		original.reserve(value.data.size());
		for(const auto & entry : value.data)
			original.push_back(entry.timestamp);
		for(auto & entry : value.data)
			entry.timestamp = 0;
	}
	~NormalizeTimestamps()
	{
		for(size_t index = 0; index < original.size(); ++index)
			statistics.data[index].timestamp = original[index];
	}
};
}

Evidence capture(CGameHandler & handler)
{
	requireUntimedAdventure(*handler.gs);
	if(handler.gs->actingPlayers.size() != 1
		|| !handler.gs->players.at(*handler.gs->actingPlayers.begin()).human)
		throw std::runtime_error("Inspection requires the sole human player at an ordinary turn boundary");
	Evidence result;
	result.day = handler.gs->day;
	result.actingPlayers = handler.gs->actingPlayers;
	result.chapters = handler.gs->replayLog.getChapters();
	result.channels["world"] = handler.gs->saveToMemory();
	result.channels["replay"] = bytes(handler.gs->replayLog);
	result.channels["gameRandomizer"] = bytes(*handler.randomizer);
	result.channels["heroPoolRandomizers"] = bytes(*handler.heroPool);
	result.rawStatistics = bytes(*handler.statistics);
	NormalizeTimestamps normalization(*handler.statistics);
	result.channels["statisticsWithoutTimestamps"] = bytes(*handler.statistics);
	result.channels["fullServerWithoutStatisticsTimestamps"] = bytes(handler);
	return result;
}

std::string firstDifference(const Evidence & left, const Evidence & right)
{
	if(left.day != right.day || left.actingPlayers != right.actingPlayers)
		return "day-or-acting-players";
	if(left.channels.size() != right.channels.size())
		return "channel-count";
	for(const auto & [name, data] : left.channels)
	{
		const auto found = right.channels.find(name);
		if(found == right.channels.end())
			return name + ":missing";
		const auto mismatch = std::mismatch(data.begin(), data.end(), found->second.begin(), found->second.end());
		if(mismatch.first != data.end() || mismatch.second != found->second.end())
			return name + ":byte:" + std::to_string(std::distance(data.begin(), mismatch.first));
	}
	return {};
}
}
