/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedRuleExecutionAudit.h"
namespace nativeRules::executionAudit
{
#ifdef NH_PERF_EXPERIMENTS
namespace { thread_local Counters * active = nullptr; }
bool available() noexcept { return true; }
void record(Event event) noexcept
{
	if(active) ++active->values[static_cast<size_t>(event)];
}
Scope::Scope(Counters & counters) noexcept : previous(active) { active = &counters; }
Scope::~Scope() { active = previous; }
#else
bool available() noexcept { return false; }
void record(Event) noexcept {}
Scope::Scope(Counters &) noexcept {}
Scope::~Scope() = default;
#endif
}
