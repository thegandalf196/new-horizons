/* NewHorizonsLightDamage.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"
class JsonNode;
namespace newHorizonsMagic
{
DLL_LINKAGE int32_t holyWrathRecipientMultiplier(const JsonNode & rules, SpellID spell, bool undeadOrDemonic);
}
