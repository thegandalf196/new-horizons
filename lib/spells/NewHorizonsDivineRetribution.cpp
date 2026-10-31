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
#include "../GameLibrary.h"
#include "../constants/StringConstants.h"
#include "../modding/CModHandler.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include <stdexcept>

namespace newHorizonsDivineRetribution
{
SpellID spellId(const JsonNode & savedRules)
{
	if(!LIBRARY || !LIBRARY->modh)
		throw std::runtime_error("Divine Retribution identity requires an initialized library");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		"new-horizons:divineRetribution", true);
	if(id)
		return SpellID(*id);
	const bool legacy = savedRules.isNull() || (savedRules.isStruct() && savedRules.Struct().empty());
	const bool moduleLoaded = vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
	if(!legacy || moduleLoaded)
		throw std::runtime_error("Divine Retribution identity is missing required New Horizons content");
	return SpellID();
}

int64_t recipientDamage(const CBattleInfoCallback & battle,
	const battle::Unit * recipient, int64_t rawPayout)
{
	if(!recipient || rawPayout <= 0)
		return 0;
	const auto * state = battle.getBattle();
	const JsonNode absentRules;
	const auto & rules = state ? state->getMagicRules() : absentRules;
	const SpellID spell = spellId(rules);
	if(!state || !spell.hasValue())
		return rawPayout;
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
