/*
 * SpellbookEntryPresentation.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <functional>
#include <set>
#include <string>
#include "../../lib/constants/EntityIdentifiers.h"

class CSpell;
class ITranslator;
class CGHeroInstance;
struct SpellCostBreakdown;
namespace newHorizonsMagic { struct SpellEffectPreview; }

namespace spellbookPresentation
{
using Width = std::function<int(const std::string &)>;
struct Access
{
	bool schoolLocked = false;
	std::string requirement;
};
Access access(const CGHeroInstance & hero, const CSpell & spell, const ITranslator & translator);
std::string levelAndSchools(int savedLevel, int mastery,
	const std::set<SpellSchool> & savedSchools, const ITranslator & translator);
std::string costLine(const SpellCostBreakdown & cost, const ITranslator & translator, const Width & width, int maximumWidth = 78);
std::string effectLine(const newHorizonsMagic::SpellEffectPreview & preview,
	const ITranslator & translator, const Width & width, int maximumWidth = 78);
std::string effectDetail(const newHorizonsMagic::SpellEffectPreview & preview, const ITranslator & translator);
}
