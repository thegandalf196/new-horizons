/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroActionAdmission.h"
#include "HeroActionUtility.h"
#include "PossibleSpellcast.h"
#include <cstddef>

namespace spells { class Mechanics; namespace effects { class Effect; } }
namespace arrowContinuation
{
/// Only the finite canonical single-target cast, never a projected world.
struct Outcome
{
	uint32_t target;
	BattleHex position;
	std::vector<std::byte> injury;
	std::vector<std::byte> marks;
	std::vector<std::byte> wisdomHistory;
	bool operator==(const Outcome &) const = default;
};
bool isArrow(const PossibleSpellcast & candidate);
bool resourceIndependent(const HypotheticBattle & state);
bool sourcesSupported(const HypotheticBattle & state, uint32_t target) noexcept;
bool canonicalDamageEffect(const spells::effects::Effect & effect, const spells::Mechanics & mechanics) noexcept;
/// Optional source admission; a failed query retains ordinary FULL valuation.
bool scoringSupported(const CBattleInfoCallback & state) noexcept;
/// Executes exactly one fresh castEval even when no receipt can be issued.
std::optional<Outcome> project(spells::Mechanics & mechanics, HypotheticBattle & state,
	const spells::Target & target);

/// One immutable Hero evaluation invocation; only FULL's scalar can be copied.
class Reuse
{
public:
	struct TestControl { bool enabled = true; size_t hits = 0; };
	explicit Reuse(TestControl * control = nullptr) : control(control) {}
	void clear() { entries.clear(); }
	std::optional<heroActionUtility::AbsoluteContinuationUtility> find(
		const PossibleSpellcast & candidate, const heroActionProjection::Admission & admission,
		const std::vector<uint32_t> & recipients, const std::optional<Outcome> & outcome,
		const HypotheticBattle & state, bool full);
	void remember(const PossibleSpellcast & candidate, const heroActionProjection::Admission & admission,
		const std::vector<uint32_t> & recipients, const std::optional<Outcome> & outcome,
		heroActionUtility::AbsoluteContinuationUtility value, const HypotheticBattle & state);
private:
	struct Entry
	{
		PossibleSpellcast options;
		HypotheticBattle::ProjectedSpellAllowance allowance;
		HypotheticBattle::ProjectedCounterspellOutcome counterspell;
		std::vector<uint32_t> recipients;
		Outcome outcome;
		heroActionUtility::AbsoluteContinuationUtility value;
	};
	bool admitted = false;
	TestControl * control;
	std::vector<Entry> entries;
};
}
