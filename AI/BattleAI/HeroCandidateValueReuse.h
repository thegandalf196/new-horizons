/*
 * HeroCandidateValueReuse.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "VinesCandidateValueReuse.h"
#include "LifeDrainCandidateValueReuse.h"
#include "BattleEvaluatorSpellProjection.h"
/// Explicit canonical families only; scope destruction clears incomplete work.
class HeroCandidateValueReuse
{
	VinesCandidateValueReuse vines;
	LifeDrainCandidateValueReuse lifeDrain;
	void clear() { vines.clear(); lifeDrain.clear(); }
public:
	HeroCandidateValueReuse(VinesCandidateValueReuse::TestControl * vinesControl,
		LifeDrainCandidateValueReuse::TestControl * lifeDrainControl)
		: vines(vinesControl), lifeDrain(lifeDrainControl) {}
	class Candidate
	{
		HeroCandidateValueReuse & owner;
		PossibleSpellcast & cast;
		bool completed = false;
	public:
		Candidate(HeroCandidateValueReuse & owner, PossibleSpellcast & cast);
		Candidate(const Candidate &) = delete;
		Candidate & operator=(const Candidate &) = delete;
		~Candidate();
		bool find(const heroActionProjection::Admission & admission, const std::vector<uint32_t> & recipients,
			const battleEvaluatorProjection::SpellProjection & projection, const CBattleInfoCallback & state);
		void remember(const heroActionProjection::Admission & admission, const std::vector<uint32_t> & recipients,
			const battleEvaluatorProjection::SpellProjection & projection, const CBattleInfoCallback & state);
	};
	Candidate beginCandidate(PossibleSpellcast & cast) { return Candidate(*this, cast); }
};
