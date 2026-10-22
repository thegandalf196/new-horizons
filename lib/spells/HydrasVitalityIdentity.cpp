/*
 * HydrasVitalityIdentity.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HydrasVitalityIdentity.h"
#include "../GameLibrary.h"
#include "../constants/StringConstants.h"
#include "../modding/CModHandler.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include <stdexcept>

namespace newHorizonsMagic
{
std::optional<BonusSourceID> hydrasVitalitySourceId(const JsonNode & savedRules)
{
	if(!LIBRARY || !LIBRARY->modh)
		throw std::runtime_error("Hydra effect identity requires an initialized library");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		"new-horizons:hydrasVitality", true);
	if(id)
		return BonusSourceID(SpellID(*id));
	const bool legacy = savedRules.isNull() || (savedRules.isStruct() && savedRules.Struct().empty());
	const bool moduleLoaded = vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
	if(!legacy || moduleLoaded)
		throw std::runtime_error("Hydra effect identity is missing required New Horizons content");
	return std::nullopt;
}

CSelector hydrasVitalitySource(const JsonNode & savedRules)
{
	const auto id = hydrasVitalitySourceId(savedRules);
	return id ? Selector::source(BonusSource::SPELL_EFFECT, *id) : Selector::none;
}
}
