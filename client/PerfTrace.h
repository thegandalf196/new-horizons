/*
 * New Horizons opt-in performance diagnostics.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <cstdint>

struct CPackForClient;
struct CPackForServer;

namespace PerfTrace
{
#ifdef NH_PERF_EXPERIMENTS
bool enabled();
void emit(const char * stage, int64_t detail = -1, int64_t value = -1);
void submitted(const CPackForServer & pack);
void requestRoute(const char * stage, int64_t request, int64_t player);
void acknowledged(int64_t request, int64_t player, int64_t result);
void visualChanged(int64_t object);
void objectDrawn(int64_t object);
void frameBegin();
void presented();
void flush();

class EventScope
{
	uint64_t previous = 0;
public:
	EventScope(uint32_t type, uint64_t timestamp, bool receipt);
	~EventScope();
};

class ApplyScope
{
	uint64_t token = 0;
	const CPackForClient & pack;
public:
	explicit ApplyScope(const CPackForClient & pack);
	void stateApplied();
	~ApplyScope();
};

class Activity
{
	const char * end;
public:
	Activity(const char * begin, const char * end);
	~Activity();
};
#else
inline bool enabled() { return false; }
inline void emit(const char *, int64_t = -1, int64_t = -1) {}
inline void submitted(const CPackForServer &) {}
inline void requestRoute(const char *, int64_t, int64_t) {}
inline void acknowledged(int64_t, int64_t, int64_t) {}
inline void visualChanged(int64_t) {}
inline void objectDrawn(int64_t) {}
inline void frameBegin() {}
inline void presented() {}
inline void flush() {}
class EventScope
{
public:
	EventScope(uint32_t, uint64_t, bool) {}
};
class ApplyScope
{
public:
	explicit ApplyScope(const CPackForClient &) {}
	void stateApplied() {}
};
class Activity
{
public:
	Activity(const char *, const char *) {}
};
#endif
}
