/*
 * LifeDrainCandidateValueReuse.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "HeroActionAdmission.h"
#include "PossibleSpellcast.h"
#include "../../lib/spells/NewHorizonsLifeDrainProjection.h"
/// One prior final value per serial Hero Action invocation. Never caches a battle,
/// skips a cast, changes target enumeration or outlives its frozen parent/caster.
class LifeDrainCandidateValueReuse
{
public:
	struct TestControl { bool enabled = true; size_t hits = 0; size_t completedReceipts = 0; };
	explicit LifeDrainCandidateValueReuse(TestControl * control = nullptr) : control(control) {}
	void clear() { entry.reset(); }
	void beginCandidate(const PossibleSpellcast & candidate);
	std::optional<float> find(const PossibleSpellcast & candidate,
		const heroActionProjection::Admission & admission, const std::vector<uint32_t> & sharedPurposeRecipients,
		const std::optional<newHorizonsLifeDrain::ProjectionReceipt> & receipt);
	void remember(const PossibleSpellcast & candidate, const heroActionProjection::Admission & admission,
		const std::vector<uint32_t> & sharedPurposeRecipients,
		const std::optional<newHorizonsLifeDrain::ProjectionReceipt> & receipt);
private:
	struct Entry
	{
		PossibleSpellcast candidate;
		HypotheticBattle::ProjectedSpellAllowance allowance;
		HypotheticBattle::ProjectedCounterspellOutcome counterspell;
		newHorizonsLifeDrain::ProjectionReceipt receipt;
	};
	bool supported(const PossibleSpellcast & candidate, const heroActionProjection::Admission & admission,
		const std::vector<uint32_t> & sharedPurposeRecipients,
		const std::optional<newHorizonsLifeDrain::ProjectionReceipt> & receipt) const;
	TestControl * control;
	std::optional<Entry> entry;
};
