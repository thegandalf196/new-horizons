/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../constants/EntityIdentifiers.h"
#include <optional>
class CCreature;
class CGHeroInstance;
class CHero;
class JsonNode;
namespace newHorizonsCreatures
{
/// Read the creature definition, never a commanded stack's inherited bonuses.
DLL_LINKAGE bool hasIntrinsicAssimilation(const CCreature & creature);
DLL_LINKAGE bool hasIntrinsicAssimilation(CreatureID creature);
/// Resolved specialty line, authored SPECIAL_UPGRADE target (not sources), or
/// fixed creature-stat limiter. Never faction permission or an actual tier change.
DLL_LINKAGE bool isCommandSpecialistCreature(const CGHeroInstance & hero, CreatureID creature);
/// Same type-aware eligibility before bonuses attach, for creation/projection.
DLL_LINKAGE bool isCommandSpecialistCreature(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, std::optional<CreatureID> target, CreatureID creature);
}
