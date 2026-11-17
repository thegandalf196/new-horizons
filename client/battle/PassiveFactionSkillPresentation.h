/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleSide.h"
#include <optional>
#include <string>
class CBattleInfoCallback;
class CGHeroInstance;
namespace passiveFactionSkillPresentation
{
struct SkillStatusText
{
	std::string value;
	std::string details;
	bool replacesProviderDescription = false;
};
std::optional<SkillStatusText> elementalAttunement(const CGHeroInstance & hero,
	const CBattleInfoCallback & battle, BattleSide side);
std::optional<SkillStatusText> divineMandate(const CGHeroInstance & hero,
	const CBattleInfoCallback & battle, BattleSide side);
std::optional<SkillStatusText> metamagic(const CGHeroInstance & hero);
}
