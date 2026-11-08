/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/constants/EntityIdentifiers.h"
#include <cstdint>
#include <memory>
#include <vector>
enum class HeroCommand : int8_t;
namespace battleAIDiagnostic
{
/// DIAGNOSTIC ONLY: already resolved IDs, not captured gameplay rules.
struct Spec
{
	bool suppressAllOrders = false;
	bool suppressAllManualHeroSpells = false;
	std::vector<HeroCommand> orders;
	std::vector<SpellID> manualHeroSpells;
};
struct Counts
{
	static constexpr const char * OUTPUT_KIND = "DIAGNOSTIC_AI_ADMISSION";
	uint64_t spellRowsVisited = 0, spellRowsPruned = 0;
	uint64_t orderKindsVisited = 0, orderKindsPruned = 0;
};
class DLL_LINKAGE Mask final
{
	const Spec spec;
	explicit Mask(Spec spec);
public:
	/// Validates once against the loaded registry. Empty returns null, without allocation.
	static std::shared_ptr<const Mask> prepare(Spec spec);
	bool suppresses(HeroCommand command) const;
	bool suppresses(SpellID spell) const;
	void requireSupportedContext(bool mandatoryOrder, bool pendingSpellFollowup) const;
};
/// Call-local counters; no live state or persistent counter ownership.
class DLL_LINKAGE Admission final
{
	const Mask & mask;
	Counts & counts;
public:
	Admission(const Mask & mask, Counts & counts) : mask(mask), counts(counts) {}
	bool permits(HeroCommand command) const;
	bool permits(SpellID spell) const;
	void requireSupportedContext(bool mandatoryOrder, bool pendingSpellFollowup) const;
};
class Observer
{
public:
	virtual ~Observer() = default;
	virtual void completedHeroAdmission(const Counts & counts) = 0;
};
}
