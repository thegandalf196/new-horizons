/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "DiagnosticBattleAIFactory.h"
#include "BattleAI.h"
#include "BattleSearchPolicy.h"
#include "../../lib/callback/AIFactory.h"
#include <stdexcept>
std::shared_ptr<CBattleGameInterface> battleAIDiagnostic::createBattleAI(const std::string & name,
	Spec spec, Observer * observer)
{
	const auto profile = battleSearch::profileForAIName(name);
	if(!profile || (*profile != battleSearch::Profile::Full && *profile != battleSearch::Profile::BeamOneTwoActors))
		throw std::invalid_argument("DIAGNOSTIC admission supports only normal Full and Quick BattleAI");
	const auto mask = Mask::prepare(std::move(spec));
	if(!mask) return AIFactory::createBattleAI(name);
	return std::make_shared<CBattleAI>(nullptr, nullptr, *profile, mask, observer);
}
