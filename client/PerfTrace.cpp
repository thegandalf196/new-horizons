/*
 * New Horizons opt-in performance diagnostics.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "StdInc.h"
#include "PerfTrace.h"

#ifdef NH_PERF_EXPERIMENTS
#include "../lib/network/PerformanceTrace.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/networkPacks/PacksForServer.h"
#include "../lib/VCMIDirs.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <thread>

namespace PerfTrace
{
namespace
{
thread_local uint64_t currentEvent = 0;
thread_local uint64_t currentFrame = 0;
thread_local unsigned callbackDepth = 0;

struct Record
{
	nhperf::TraceRecord common;
	uint64_t event = 0;
	uint64_t apply = 0;
	uint64_t frame = 0;
	int64_t detail = -1;
	int64_t value = -1;
	int64_t matchedRequest = -1;
	const char * packetType = nullptr;
};
struct Event
{
	uint64_t token = 0;
	uint64_t timestamp = 0;
	uint32_t type = 0;
};
struct Move
{
	int64_t request = -1;
	int64_t hero = -1;
	int3 destination;
	uint64_t event = 0;
	uint64_t submitted = 0;
};
struct Visual
{
	int64_t hero = -1;
	int64_t matchedRequest = -1;
	uint64_t event = 0;
	uint64_t apply = 0;
	uint64_t frame = 0;
	bool changed = false;
};
struct Storage
{
	std::mutex mutex;
	std::vector<Record> records;
	std::array<Event, 4096> events{};
	std::array<Move, 64> moves{};
	std::array<Visual, 64> visuals{};
	uint64_t next = 0;
	uint64_t tokens = 0;
	uint64_t frames = 0;
	uint64_t dropped = 0;
	uint64_t metadataEvicted = 0;
	bool stopped = false;
	size_t capacity = 250000;
	std::string outputPath;

	Storage()
	{
		const char * configured = std::getenv("NH_PERF_TRACE_FILE");
		outputPath = configured ? configured : (VCMIDirs::get().userLogsPath() / "nh-perf-client.jsonl").string();
		if(const char * limit = std::getenv("NH_PERF_TRACE_MAX_RECORDS"))
		{
			char * end = nullptr;
			auto requested = std::strtoull(limit, &end, 10);
			if(end != limit && *end == '\0' && requested >= 1024 && requested <= 1000000)
				capacity = static_cast<size_t>(requested);
		}
		records.reserve(capacity);
	}
	Record record(const char * stage)
	{
		Record result;
		result.common.steady_ns = nhperf::nowNs();
		result.common.thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
		result.common.emitter = "client";
		result.common.stage = stage;
		result.event = currentEvent;
		result.frame = currentFrame;
		return result;
	}
	void add(Record record)
	{
		if(stopped)
			return;
		record.common.seq = ++next;
		if(records.size() < capacity)
			records.push_back(record);
		else
			++dropped;
	}
};
Storage & storage()
{
	// Process lifetime: global engine destruction must not access an already
	// destroyed function-static buffer or directory singleton. flush is explicit.
	static Storage * instance = new Storage;
	return *instance;
}
}

bool enabled()
{
	static const bool active = []
	{
		const char * value = std::getenv("NH_PERF_TRACE");
		return value && std::strcmp(value, "1") == 0;
	}();
	return active;
}

void emit(const char * stage, int64_t detail, int64_t value)
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	auto record = s.record(stage);
	record.detail = detail;
	record.value = value;
	s.add(record);
}

EventScope::EventScope(uint32_t type, uint64_t timestamp, bool receipt)
{
	if(!enabled())
		return;
	previous = currentEvent;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	currentEvent = 0;
	if(receipt)
	{
		currentEvent = ++s.tokens;
		auto & slot = s.events[currentEvent % s.events.size()];
		if(slot.token)
			++s.metadataEvicted;
		slot = Event{currentEvent, timestamp, type};
	}
	else
	{
		unsigned matches = 0;
		for(auto & event : s.events)
		{
			if(event.token && event.type == type && event.timestamp == timestamp)
			{
				currentEvent = event.token;
				event.token = 0;
				++matches;
			}
		}
		// SDL2 millisecond timestamps can collide; coalescing can also lose events.
		// Never fabricate a unique receipt->dispatch link for ambiguous keys.
		if(matches != 1)
			currentEvent = 0;
	}
	auto record = s.record(receipt ? "event_received" : "event_dispatch");
	record.detail = type;
	record.value = static_cast<int64_t>(timestamp);
	s.add(record);
}
EventScope::~EventScope()
{
	if(enabled())
		currentEvent = previous;
}

void submitted(const CPackForServer & pack)
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	auto record = s.record("command_submit");
	record.common.request_id = pack.requestID;
	record.common.player = pack.player.getNum();
	record.packetType = typeid(pack).name();
	record.detail = callbackDepth;
	s.add(record);
	if(const auto * move = dynamic_cast<const MoveHero *>(&pack); move && !move->path.empty())
	{
		auto & slot = s.moves[pack.requestID % s.moves.size()];
		if(slot.request >= 0)
			++s.metadataEvicted;
		slot = Move{pack.requestID, move->hid.getNum(), move->path.front(), currentEvent, record.common.steady_ns};
	}
}

ApplyScope::ApplyScope(const CPackForClient & pack) : pack(pack)
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	token = ++s.tokens;
	auto record = s.record("client_apply_begin");
	record.apply = token;
	record.packetType = typeid(pack).name();
	record.detail = ++callbackDepth;
	s.add(record);
}
void ApplyScope::stateApplied()
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	auto record = s.record("client_state_applied");
	record.apply = token;
	record.packetType = typeid(pack).name();
	s.add(record);
	const auto * result = dynamic_cast<const TryMoveHero *>(&pack);
	if(!result || result->result != TryMoveHero::SUCCESS || result->start == result->end)
		return;
	Move * match = nullptr;
	unsigned matches = 0;
	for(auto & move : s.moves)
	{
		if(move.request >= 0 && move.hero == result->id.getNum() && move.destination == result->end &&
			record.common.steady_ns - move.submitted < 30000000000ULL)
		{
			match = &move;
			++matches;
		}
	}
	// A newer result for the same hero can replace an unpresented intermediate
	// state. Do not attribute the later draw to both commands.
	for(auto & pending : s.visuals)
	{
		if(pending.hero == result->id.getNum())
		{
			auto superseded = s.record("visual_superseded");
			superseded.apply = pending.apply;
			s.add(superseded);
			pending = Visual{};
		}
	}
	auto & visual = s.visuals[token % s.visuals.size()];
	if(visual.hero >= 0)
		++s.metadataEvicted;
	visual = Visual{};
	visual.hero = result->id.getNum();
	visual.apply = token;
	if(matches == 1)
	{
		visual.matchedRequest = match->request;
		visual.event = match->event;
		match->request = -1;
	}
}
ApplyScope::~ApplyScope()
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	auto record = s.record("client_apply_end");
	record.apply = token;
	record.packetType = typeid(pack).name();
	record.detail = callbackDepth--;
	s.add(record);
}

void acknowledged(int64_t request, int64_t player, int64_t result)
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	auto record = s.record("ack");
	record.common.request_id = request;
	record.common.player = player;
	record.detail = result;
	s.add(record);
	for(auto & move : s.moves)
		if(move.request == request)
			move.request = -1;
}

void visualChanged(int64_t object)
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	for(auto & visual : s.visuals)
		if(visual.hero == object)
			visual.changed = true;
}
void objectDrawn(int64_t object)
{
	if(!enabled() || !currentFrame)
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	for(auto & visual : s.visuals)
	{
		if(visual.hero == object && visual.changed && !visual.frame)
		{
			visual.frame = currentFrame;
			auto record = s.record("affected_object_draw_candidate");
			record.apply = visual.apply;
			record.event = visual.event;
			record.matchedRequest = visual.matchedRequest;
			s.add(record);
		}
	}
}
void frameBegin()
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	currentFrame = ++s.frames;
	s.add(s.record("frame_begin"));
}
void presented()
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	s.add(s.record("present_return"));
	for(auto & visual : s.visuals)
	{
		if(visual.hero >= 0 && visual.frame == currentFrame && currentFrame)
		{
			auto record = s.record("affected_present_candidate");
			record.apply = visual.apply;
			record.event = visual.event;
			record.matchedRequest = visual.matchedRequest;
			s.add(record);
			visual = Visual{};
		}
	}
	currentFrame = 0;
}

Activity::Activity(const char * begin, const char * end) : end(end)
{
	if(enabled())
		emit(begin, ++callbackDepth);
}
Activity::~Activity()
{
	if(enabled())
		emit(end, callbackDepth--);
}

void flush()
{
	if(!enabled())
		return;
	auto & s = storage();
	std::lock_guard lock(s.mutex);
	if(s.stopped)
		return;
	s.stopped = true;
	std::ofstream out(s.outputPath, std::ios::out | std::ios::trunc);
	if(!out)
	{
		std::cerr << "NH performance trace could not be written at shutdown\n";
		return;
	}
	out << "{\"schema\":\"nh-perf-v1\",\"emitter\":\"client\",\"clock\":\"steady_clock_epoch_ns_same_process_only\",\"capacity\":" << s.capacity
		<< ",\"dropped\":" << s.dropped << ",\"metadata_evicted\":" << s.metadataEvicted << "}\n";
	for(const auto & r : s.records)
	{
		out << "{\"schema\":\"nh-perf-v1\",\"emitter\":\"client\",\"stage\":" << std::quoted(r.common.stage)
			<< ",\"steady_ns\":" << r.common.steady_ns << ",\"seq\":" << r.common.seq << ",\"thread\":" << r.common.thread;
		if(r.common.request_id >= 0) out << ",\"request_id\":" << r.common.request_id;
		if(r.common.player >= 0) out << ",\"player\":" << r.common.player;
		if(r.event) out << ",\"event_id\":" << r.event;
		if(r.apply) out << ",\"apply_id\":" << r.apply;
		if(r.frame) out << ",\"frame_id\":" << r.frame;
		if(r.detail >= 0) out << ",\"detail\":" << r.detail;
		if(r.value >= 0) out << ",\"value\":" << r.value;
		if(r.matchedRequest >= 0) out << ",\"matched_request_id\":" << r.matchedRequest;
		if(r.packetType) out << ",\"packet_type\":" << std::quoted(r.packetType);
		out << "}\n";
	}
}
}
#endif
