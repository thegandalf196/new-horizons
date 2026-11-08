/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class JsonNode;
namespace newHorizonsHeroes
{
DLL_LINKAGE void validatePassiveSpecialtyPolicy(const JsonNode & policy);
DLL_LINKAGE void validatePassiveSpecialtySerialization(const JsonNode & development, bool supported);
}
