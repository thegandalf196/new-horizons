/*
 * PerformanceTrace.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <chrono>
#include <cstdint>

// nh-perf-v1 shared clock/record vocabulary; storage and opt-in lifetime belong to
// each emitter. No packet changes, implicit disk writes or global delivery policy.
namespace nhperf
{
using Clock = std::chrono::steady_clock;

inline uint64_t nowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

// Process identity, source/build identity and clock-domain description belong in
// the run manifest. seq is emitter-local, NOT a gameplay command identifier.
// stage/emitter must point to static literals when records are buffered.
// Negative identifiers mean unknown and must be omitted from exported records.
struct DLL_LINKAGE TraceRecord
{
	uint64_t steady_ns = 0;
	uint64_t seq = 0;
	uint64_t thread = 0;
	const char * emitter = nullptr;
	const char * stage = nullptr;
	int64_t request_id = -1;
	int64_t player = -1;
	int64_t battle_id = -1;
	uint64_t packet_bytes = 0;
};

// Emitters must default OFF, bound preallocated storage, expose dropped records,
// and flush outside measured events. Compare OFF/ON overhead. Event receipt,
// submission, authoritative apply, client apply, ACK and presentation are distinct
// stages. A clock correlation is required for external injection timestamps;
// neither ACK nor an arbitrary subsequent frame proves affected presentation.
}
