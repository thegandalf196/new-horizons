/*
 * NewHorizonsDivineRetribution.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsDivineRetribution.h"

#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"

namespace newHorizonsDivineRetribution
{
int64_t recipientDamage(const CBattleInfoCallback & battle,
	const battle::Unit * recipient, int64_t rawPayout)
{
	if(!recipient || rawPayout <= 0)
		return 0;
	const auto * state = battle.getBattle();
	const SpellID spell(SpellID::decode("new-horizons:divineRetribution"));
	if(!state || !spell.hasValue())
		return rawPayout;
	const auto & rules = state->getMagicRules();
	if(!newHorizonsMagic::rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !newHorizonsMagic::spellAllowedBySavedRoster(rules, spell))
		return rawPayout;
	// A delayed judgment is not another cast. Its cap already captures the
	// original SP scaling; do not synthesize a caster or apply that scaling again.
	return spell.toSpell()->adjustRawDamage(nullptr, recipient, rawPayout, 0,
		battle.battleGetHoldTheLineMagicalReductionBasisPoints(recipient), 100,
		battle.battleUsesNewHorizonsMultiplicativeMDR(), true, false,
		battle.battleGetPerkMagicalReductionBasisPoints(recipient));
}
}
