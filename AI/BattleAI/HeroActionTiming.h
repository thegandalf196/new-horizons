/*
 * HeroActionTiming.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace battle { class Unit; }

namespace heroActionTiming
{
using Clock = std::chrono::steady_clock;
using Now = Clock::time_point (*)();

struct DLL_LINKAGE Stage
{
	uint64_t calls = 0;
	uint64_t totalMicroseconds = 0;
	uint64_t maxMicroseconds = 0;
	void accumulate(const Stage & other);
};

struct Continuation
{
	Stage full;
	Stage cached;
	// Sequential child scopes within one FULL body; different candidates overlap.
	Stage fullSetup;
	Stage fullReachability;
	Stage fullMovement;
	Stage fullBestTarget;
};

/// Worker slots overlap in wall time. Their sums are not disjoint loop time.
struct Summary
{
	// Caller wall includes candidate loop/join and post-join diagnostic slot reduction.
	Stage candidateLoop;
	Continuation continuation;
	Stage submit;
	uint64_t candidates = 0;
	uint64_t submitReturned = 0;
	uint64_t submitThrew = 0;
	bool loopJoined = false;
	bool loopThrew = false;
};

class DLL_LINKAGE Scope
{
	Stage * stage;
	Now now;
	Clock::time_point started;
public:
	explicit Scope(Stage * stage, Now now = &Clock::now);
	~Scope();
	Scope(const Scope &) = delete;
	Scope & operator=(const Scope &) = delete;
};

/// Each worker owns only its candidate index. Reduce only after TBB joins.
class DLL_LINKAGE Candidates
{
	std::vector<Continuation> slots;
public:
	void prepare(bool enabled, size_t count);
	Continuation * at(size_t index);
	void finish(Summary & summary, bool threw) const;
	size_t size() const { return slots.size(); }
};

class DLL_LINKAGE LoopScope
{
	Candidates & candidates;
	Summary * summary;
	int exceptions;
	Scope wall;
public:
	LoopScope(Candidates & candidates, Summary * summary, Now now = &Clock::now);
	~LoopScope();
};

class DLL_LINKAGE SubmitScope
{
	Summary * summary;
	int exceptions;
	Scope wall;
public:
	explicit SubmitScope(Summary * summary, Now now = &Clock::now);
	~SubmitScope();
};

struct Identity
{
	int battle = -1;
	int player = -1;
	int side = -1;
	int round = -1;
	int64_t stack = -1;
};

DLL_LINKAGE unsigned int parseLimit(const char * value);
DLL_LINKAGE int64_t captureStackId(const battle::Unit * stack);

/// Local diagnostic only: never retains an evaluator, callback or gameplay node.
class DLL_LINKAGE Call
{
	bool requested = false;
	uint64_t ordinal = 0;
	Identity identity;
	Summary summary;
	Candidates candidates;
public:
	Call();
	~Call();
	Call(const Call &) = delete;
	Call & operator=(const Call &) = delete;
	bool enabled() const { return requested; }
	void identify(Identity captured) { identity = captured; }
	void prepare(size_t count);
	Continuation * continuation(size_t index);
	Summary * sink() { return requested ? &summary : nullptr; }
	Candidates & candidateSlots() { return candidates; }
};
}
