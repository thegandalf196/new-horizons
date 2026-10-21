/*
 * VinesCandidateValueReuse.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "HeroActionAdmission.h"
#include "PossibleSpellcast.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"

/// Invocation-local final values, never projected states or representative aims.
/// Construct inside one serial candidate loop. The parent/caster/active actor,
/// evaluator, horizon and cached action must remain fixed for its lifetime.
/// Each candidate must run its own admission, projection and accepted bookkeeping
/// before lookup. Clear on unsupported candidates: they can warm shared caches.
class VinesCandidateValueReuse
{
public:
	static bool scoringScriptsSupported(const CBattleInfoCallback & battle);
	/// Same-binary test injection only; no runtime preference or retained observer.
	struct TestControl { bool enabled = true; size_t hits = 0; };
	explicit VinesCandidateValueReuse(TestControl * control = nullptr) : control(control) {}
	VinesCandidateValueReuse(const VinesCandidateValueReuse &) = delete;
	VinesCandidateValueReuse & operator=(const VinesCandidateValueReuse &) = delete;
	void clear() { entries.clear(); }
	void beginCandidate(const PossibleSpellcast & candidate);
	std::optional<float> find(const PossibleSpellcast & candidate,
		const heroActionProjection::Admission & admission,
		const std::vector<uint32_t> & sharedPurposeRecipients,
		const std::optional<newHorizonsVengefulVines::ProjectionReceipt> & receipt);
	void remember(const PossibleSpellcast & candidate,
		const heroActionProjection::Admission & admission,
		const std::vector<uint32_t> & sharedPurposeRecipients,
		const std::optional<newHorizonsVengefulVines::ProjectionReceipt> & receipt);
private:
	struct Entry
	{
		PossibleSpellcast candidate;
		HypotheticBattle::ProjectedSpellAllowance allowance;
		HypotheticBattle::ProjectedCounterspellOutcome counterspell;
		std::vector<uint32_t> sharedPurposeRecipients;
		newHorizonsVengefulVines::ProjectionReceipt receipt;
	};
	bool supported(const PossibleSpellcast & candidate,
		const heroActionProjection::Admission & admission,
		const std::optional<newHorizonsVengefulVines::ProjectionReceipt> & receipt) const;
	TestControl * control;
	std::vector<Entry> entries;
};
