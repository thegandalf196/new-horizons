/*
 * SpellbookEntryPresentation.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <functional>
#include <optional>
#include <vector>
#include "../../lib/spells/RecipientSpellEffectPreview.h"
#include <set>
#include <string>
#include "../../lib/constants/EntityIdentifiers.h"

class CSpell;
class ITranslator;
class CGHeroInstance;
class CBattleInfoCallback;
struct SpellCostBreakdown;
namespace newHorizonsMagic { struct SpellEffectPreview; }

namespace spellbookPresentation
{
/// Read-only current-rules Heavenly Gale help; no pending selection or cast is created.
struct ArmySpellRecipient
{
	uint32_t unitId;
	std::string name;
	int64_t count;
	int16_t hex;
	newHorizonsMagic::RecipientSpellEffectPreview effect;
};
struct GaleArmyPreview
{
	int32_t manaCost;
	bool canCast;
	std::vector<std::string> problems;
	std::vector<ArmySpellRecipient> recipients;
};
std::optional<GaleArmyPreview> heavenlyGaleArmyPreview(const CGHeroInstance & hero,
	const CSpell & spell, const CBattleInfoCallback * battle);
std::string armyEffectDetail(const GaleArmyPreview & preview, const ITranslator & translator);
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
/// Exact compact wording for recipient previews; quantities are never rounded for width.
std::string effectSummary(const newHorizonsMagic::SpellEffectPreview & preview, const ITranslator & translator);
std::string effectDetail(const newHorizonsMagic::SpellEffectPreview & preview, const ITranslator & translator);
}
