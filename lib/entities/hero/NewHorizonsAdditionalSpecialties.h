/*
 * NewHorizonsAdditionalSpecialties.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "NewHorizonsOrdinaryCreatureSpecialties.h"
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "../../constants/EntityIdentifiers.h"
#include <optional>
class JsonNode;
class CGHeroInstance;
class CHero;
struct BattleAttackInfo;
class CBattleInfoCallback;
struct Bonus;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
struct CreatureLineSpecialtyRules;
/// Pure construction; hero instance retains all bonus-tree mutation and marker ownership.
DLL_LINKAGE std::vector<std::shared_ptr<Bonus>> creatureLineSpecialtyBonuses(const CGHeroInstance & hero,
	const CreatureLineSpecialtyRules & rules, CreatureID creature, const std::array<std::string, 4> & markers);
struct DLL_LINKAGE FieldPhysicianRules
{
	int healingPercentPerLevel = 0;
	int healingMaximumPercent = 0;
};
struct DLL_LINKAGE ManaFurnaceRules
{
	int restorePointsPerLevel = 0;
	int maximumRestorePoints = 0;
};
DLL_LINKAGE std::optional<ManaFurnaceRules> manaFurnaceRules(const JsonNode & development);
DLL_LINKAGE int32_t manaFurnaceRestoration(const CGHeroInstance * hero, const ManaFurnaceRules & rules);
DLL_LINKAGE bool resolveManaFurnacePresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
DLL_LINKAGE bool usesConvertedCreatureSpecialty(const CGHeroInstance * hero);
DLL_LINKAGE bool replacesGenericCreatureSpecialty(const CGHeroInstance * hero);
DLL_LINKAGE void validateAdditionalSpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE std::optional<FieldPhysicianRules> fieldPhysicianRules(const JsonNode & development);
DLL_LINKAGE int fieldPhysicianHealingPercent(const CGHeroInstance * hero);
DLL_LINKAGE int64_t fieldPhysicianHealing(int64_t ordinary, const CGHeroInstance * hero);
DLL_LINKAGE bool resolveAdditionalSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
