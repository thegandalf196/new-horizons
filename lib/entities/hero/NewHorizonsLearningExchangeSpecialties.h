/*
 * NewHorizonsLearningExchangeSpecialties.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../constants/EntityIdentifiers.h"
#include <optional>
#include <vector>
class JsonNode;
class CHero;
class CGHeroInstance;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
enum class LearningExchangeSpecialtyKind { ECUMENICAL_SCHOLAR, OCCULT_SCHOLAR };
struct DLL_LINKAGE LearningExchangeSpecialtyRules
{
	int learningPercentPerLevel = 0;
	int learningMaximumPercent = 0;
	int additionalSpellsTaught = 0;
	int additionalSpellsReceived = 0;
};
struct DLL_LINKAGE ScholarExchange
{
	std::vector<SpellID> firstTeaches;
	std::vector<SpellID> secondTeaches;
	bool directedBonus = false;
};
DLL_LINKAGE void validateLearningExchangeSpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE std::optional<LearningExchangeSpecialtyRules> learningExchangeSpecialtyRules(
	const JsonNode & development, LearningExchangeSpecialtyKind kind);
DLL_LINKAGE std::optional<LearningExchangeSpecialtyRules> learningExchangeSpecialtyRules(const CGHeroInstance * hero);
/// Budget alone grants no meeting permission; the ordinary legality predicate still applies.
DLL_LINKAGE int scholarExchangeSpellBudget(const CGHeroInstance & teacher, const CGHeroInstance & recipient);
/// Pure pre-meeting books and ordinary legality; no receipt or spell mutation.
DLL_LINKAGE std::optional<ScholarExchange> prepareScholarExchange(
	const CGHeroInstance & first, const CGHeroInstance & second, int32_t week);
DLL_LINKAGE bool resolveLearningExchangePresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
