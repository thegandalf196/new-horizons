/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace nativeRules::executionAudit
{
enum class Event { DevelopmentAdmission, MasteryAdmission, CapabilityAdmission, IdentifierResolution, MetamagicAdmission, MetamagicEnergyAdmission, Count };
struct DLL_LINKAGE Counters
{
	std::array<uint64_t, static_cast<size_t>(Event::Count)> values{};
	uint64_t count(Event event) const { return values.at(static_cast<size_t>(event)); }
	bool operator==(const Counters &) const = default;
};
/// Verification of work, not elapsed time. Disabled builds have no call-site work.
DLL_LINKAGE bool available() noexcept;
DLL_LINKAGE void record(Event) noexcept;
class DLL_LINKAGE Scope final
{
	Counters * previous = nullptr;
public:
	explicit Scope(Counters &) noexcept;
	~Scope();
	Scope(const Scope &) = delete;
	Scope & operator=(const Scope &) = delete;
};
}
#ifdef NH_PERF_EXPERIMENTS
#define NH_CAPTURE_RULE_WORK(event) ::nativeRules::executionAudit::record(::nativeRules::executionAudit::Event::event)
#else
#define NH_CAPTURE_RULE_WORK(event) ((void)0)
#endif
