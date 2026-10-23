/*
 * HeroCandidateValueReuse.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCandidateValueReuse.h"
namespace
{
battleEvaluatorProjection::SpellProjection checked(
	const battleEvaluatorProjection::SpellProjection & projection, const CBattleInfoCallback & state)
{
	auto result = projection;
	if((result.vines || result.lifeDrain) && !VinesCandidateValueReuse::scoringScriptsSupported(state))
	{
		result.vines.reset();
		result.lifeDrain.reset();
	}
	return result;
}
}
HeroCandidateValueReuse::Candidate::Candidate(HeroCandidateValueReuse & owner, PossibleSpellcast & cast)
	: owner(owner), cast(cast)
{
	owner.vines.beginCandidate(cast);
	owner.lifeDrain.beginCandidate(cast);
}
HeroCandidateValueReuse::Candidate::~Candidate()
{
	if(!completed)
		owner.clear();
}
bool HeroCandidateValueReuse::Candidate::find(const heroActionProjection::Admission & admission,
	const std::vector<uint32_t> & recipients, const battleEvaluatorProjection::SpellProjection & projection,
	const CBattleInfoCallback & state)
{
	const auto proof = checked(projection, state);
	auto value = owner.vines.find(cast, admission, recipients, proof.vines);
	if(!value)
		value = owner.lifeDrain.find(cast, admission, recipients, proof.lifeDrain);
	if(!value)
		return false;
	cast.value = *value;
	completed = true;
	return true;
}
void HeroCandidateValueReuse::Candidate::remember(const heroActionProjection::Admission & admission,
	const std::vector<uint32_t> & recipients, const battleEvaluatorProjection::SpellProjection & projection,
	const CBattleInfoCallback & state)
{
	const auto proof = checked(projection, state);
	owner.vines.remember(cast, admission, recipients, proof.vines);
	owner.lifeDrain.remember(cast, admission, recipients, proof.lifeDrain);
	completed = true;
}
