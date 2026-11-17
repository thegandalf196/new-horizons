/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class JsonNode;
namespace newHorizonsHeroes
{
/// Cold admission only: replace exact recognized obsolete presentation strings.
/// Preserve identifier scopes, override flags, gameplay values and provenance.
DLL_LINKAGE void normalizeLegacyHeroProcText(JsonNode & rules);
}
