/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../lib/StdInc.h"
#include "BattleActionExport.h"
#include "SaveParityInput.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/ReplayLog.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/serializer/CTypeList.h"
#include <map>

namespace battleActionExport
{
namespace
{
struct Span
{
	BattleID id;
	JsonNode metadata;
	bool result = false;
	bool ended = false;
	bool descriptor = false;
	int round = -1;
};
JsonNode position(size_t chapter, uint32_t day, size_t packet, size_t ordinal)
{
	JsonNode result;
	result["chapter"].Integer() = chapter;
	result["day"].Integer() = day;
	result["packet"].Integer() = packet;
	result["ordinal"].Integer() = ordinal;
	return result;
}
std::string packetScope(const JsonNode & event, BattleID selected)
{
	if(event["battleID"].isNumber())
		return event["battleID"].Integer() == selected.getNum() ? "selected_battle" : "other_battle_interleaved";
	return event["ownership"].String() == "no_battle_identity" ? "interval_context" : "unclassified";
}
void appendBounded(JsonVector & target, JsonNode value, size_t & bytes, const Limits & limits)
{
	const auto size = value.toString().size();
	if(size > limits.outputBytes || bytes > limits.outputBytes - size)
		throw std::runtime_error("Action export exceeds the bounded JSON budget");
	bytes += size;
	target.push_back(std::move(value));
}
void loadContext(CGameState * context, const ReplayChapter & chapter, bool & ready, bool openBattle)
{
	if(!context)
		return;
	if(!chapter.gamestateSnapshot.empty())
	{
		if(openBattle)
			throw std::runtime_error("A new snapshot interrupts an open battle interval");
		if(chapter.gamestateSnapshot.size() > 128 * 1024 * 1024)
			throw std::runtime_error("Replay snapshot exceeds the context budget");
		context->loadReplaySnapshot(chapter.snapshotPayload());
		saveParity::requireUntimedAdventure(*context);
		ready = true;
	}
	if(!ready || context->replayLog.isRecordingPacks() || context->getMapEventDispatcher())
		throw std::runtime_error("Historical context needs a known snapshot without recording or script dispatch");
}
void applyRecorded(CGameState * context, CPack & packet)
{
	if(!context)
		return;
	auto * clientPacket = dynamic_cast<CPackForClient *>(&packet);
	if(!clientPacket)
		throw std::runtime_error("Recorded packet is not meant for client state replay");
	// Exact library fast-forward owner used by GameplayReplayer before GUI setup.
	context->apply(*clientPacket);
	if(context->getMapEventDispatcher() || context->replayLog.isRecordingPacks())
		throw std::runtime_error("Historical replay acquired forbidden execution/recording state");
}
}
JsonNode inspect(const ReplayLog & replay, CGameState * historicalContext,
	std::optional<size_t> selected, const Limits & limits)
{
	if(limits.packets == 0 || limits.packetBytes == 0 || limits.intervalPackets == 0
		|| limits.intervals == 0 || limits.outputBytes == 0)
		throw std::runtime_error("Action export limits must be positive");
	replay.validateStorage();
	std::vector<Span> spans;
	std::map<BattleID, size_t> open;
	JsonVector events;
	size_t ordinal = 0, bytes = 0, selectedPackets = 0, orphanBoundaries = 0;
	std::optional<uint32_t> previousDay;
	bool contextReady = false;
	const auto & chapters = replay.getChapters();
	if(chapters.size() > 10000)
		throw std::runtime_error("Replay exceeds chapter count budget");
	for(size_t chapterIndex = 0; chapterIndex < chapters.size(); ++chapterIndex)
	{
		const auto & chapter = chapters[chapterIndex];
		if(previousDay && chapter.day <= *previousDay)
			throw std::runtime_error("Replay chapter days are not strictly ordered");
		previousDay = chapter.day;
		loadContext(historicalContext, chapter, contextReady, !open.empty());
		for(size_t packetIndex = 0; packetIndex < chapter.packs.size(); ++packetIndex)
		{
			if(ordinal >= limits.packets || chapter.packs[packetIndex].size() > limits.packetBytes)
				throw std::runtime_error("Replay exceeds action-export packet limits");
			const auto location = position(chapterIndex, chapter.day, packetIndex, ordinal++);
			std::unique_ptr<CPack> decoded;
			try { decoded = ReplayPackSerializer::read(chapter.packPayload(packetIndex), historicalContext); }
			catch(const std::exception & error)
			{
				throw std::runtime_error("Replay decode refused at chapter " + std::to_string(chapterIndex)
					+ " packet " + std::to_string(packetIndex) + "; no partial export: " + error.what());
			}
			const auto * start = dynamic_cast<const BattleStart *>(decoded.get());
			const auto * result = dynamic_cast<const BattleResult *>(decoded.get());
			const auto * ended = dynamic_cast<const BattleEnded *>(decoded.get());
			const auto * cancelled = dynamic_cast<const BattleCancelled *>(decoded.get());
			if(start)
			{
				if(spans.size() >= limits.intervals)
					throw std::runtime_error("Replay exceeds retained battle interval limit");
				if(start->battleID == BattleID::NONE || open.contains(start->battleID))
					throw std::runtime_error("Duplicate or invalid open BattleStart");
				Span span;
				span.id = start->battleID;
				span.descriptor = start->info != nullptr;
				span.round = start->info ? start->info->getRound() : -1;
				span.metadata["index"].Integer() = spans.size();
				span.metadata["battleID"].Integer() = start->battleID.getNum();
				span.metadata["start"] = location;
				open.emplace(start->battleID, spans.size());
				spans.push_back(std::move(span));
			}
			if(result)
			{
				const auto found = open.find(result->battleID);
				if(found == open.end())
					++orphanBoundaries;
				else
				{
					auto & span = spans[found->second];
					if(span.result)
						throw std::runtime_error("Duplicate BattleResult in one interval");
					span.result = true;
					span.metadata["result"] = location;
				}
			}
			if(selected && *selected < spans.size() && !spans[*selected].ended)
			{
				auto & span = spans[*selected];
				if(++selectedPackets > limits.intervalPackets)
					throw std::runtime_error("Selected battle exceeds the interval packet limit");
				auto event = describe(*decoded);
				if(event["battleID"].isNumber() && event["battleID"].Integer() == span.id.getNum())
					if(dynamic_cast<const BattleNextRound *>(decoded.get()))
						++span.round;
				event["scope"].String() = packetScope(event, span.id);
				event["position"] = location;
				event["packetVersion"].Integer() = static_cast<int32_t>(chapter.packVersions[packetIndex]);
				event["packetTypeID"].Integer() = CTypeList::getInstance().getTypeID(decoded.get());
				event["selectedBattleRound"].Integer() = span.round;
				appendBounded(events, std::move(event), bytes, limits);
			}
			if(ended || cancelled)
			{
				const auto id = ended ? ended->battleID : cancelled->battleID;
				const auto found = open.find(id);
				if(found == open.end())
					++orphanBoundaries;
				else
				{
					auto & span = spans[found->second];
					span.ended = true;
					span.metadata["end"] = location;
					span.metadata["cancelled"].Bool() = cancelled != nullptr;
					open.erase(found);
				}
			}
			try { applyRecorded(historicalContext, *decoded); }
			catch(const std::exception & error)
			{
				throw std::runtime_error("Recorded effect refused at chapter " + std::to_string(chapterIndex)
					+ " packet " + std::to_string(packetIndex) + "; no partial export: " + error.what());
			}
		}
	}
	JsonNode output;
	output["schema"].Integer() = 1;
	output["decodeContext"].String() = historicalContext
		? "historical snapshot and recorded effects" : "decoder fixture only";
	output["historicalContextLoaded"].Bool() = contextReady;
	output["scope"].String() = "Retained authoritative packets; no AI execution or wallclock solver timings";
	output["requestedActionPayloadsRecorded"].Bool() = false;
	output["orphanBoundaryCount"].Integer() = orphanBoundaries;
	output["scannedPackets"].Integer() = ordinal;
	for(auto & span : spans)
	{
		span.metadata["hasInitialDescriptor"].Bool() = span.descriptor;
		span.metadata["hasResult"].Bool() = span.result;
		span.metadata["hasEnd"].Bool() = span.ended && !span.metadata["cancelled"].Bool();
		span.metadata["complete"].Bool() = span.descriptor && span.result && span.ended
			&& !span.metadata["cancelled"].Bool();
	}
	if(selected)
	{
		if(*selected >= spans.size())
			throw std::runtime_error("Selected retained battle index does not exist");
		if(!spans[*selected].metadata["complete"].Bool())
			throw std::runtime_error("Selected battle lacks BattleStart descriptor, Result or Ended; incomplete export refused");
		output["interval"] = std::move(spans[*selected].metadata);
		output["events"].Vector() = std::move(events);
	}
	else
	{
		output["intervals"].Vector();
		for(auto & span : spans)
			appendBounded(output["intervals"].Vector(), std::move(span.metadata), bytes, limits);
	}
	if(output.toString().size() > limits.outputBytes)
		throw std::runtime_error("Action export exceeds the bounded JSON envelope budget");
	return output;
}
}
