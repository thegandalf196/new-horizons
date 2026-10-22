/*
 * HydrasVitalityIdentity.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../bonuses/BonusSelector.h"
#include "../json/JsonNode.h"
#include <optional>

namespace newHorizonsMagic
{
/// Resolve the installed identity even for inactive/historical effects. Absence
/// is optional only with empty legacy rules and no loaded New Horizons module.
DLL_LINKAGE std::optional<BonusSourceID> hydrasVitalitySourceId(const JsonNode & savedRules);
/// Ordinary effect-source matching; the exchange planner separately retains its
/// existing SID-only comparisons through hydrasVitalitySourceId.
DLL_LINKAGE CSelector hydrasVitalitySource(const JsonNode & savedRules);
}
