/*
 * NewHorizonsPerkHelp.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/GameLibrary.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

#include <optional>
#include <string>
#include <string_view>

namespace newHorizonsPerkHelp
{
/// Resolve a saved perk's owning Skill without assuming that the current
/// client has the same registry as the server.  Old saves and forged/unknown
/// offer identities must leave a readable popup, not throw from a right-click
/// handler.
inline std::optional<newHorizonsHeroes::SkillDefinition> skillDefinition(const CGHeroInstance * hero,
	std::string_view skillId)
{
	if(!hero)
		return std::nullopt;

	try
	{
		return newHorizonsHeroes::perkSkill(hero->getPerkState().rules, skillId);
	}
	catch(const std::exception &)
	{
		// A legacy or partially migrated rules snapshot is still allowed to
		// display a level-up offer.  Continue to the installed-entity fallback.
		return std::nullopt;
	}
}

inline std::optional<newHorizonsHeroes::PerkDefinition> perkDefinition(const CGHeroInstance * hero,
	std::string_view skillId, std::string_view perkId)
{
	if(!hero)
		return std::nullopt;

	try
	{
		return newHorizonsHeroes::perkDefinition(hero->getPerkState().rules, skillId, perkId);
	}
	catch(const std::exception &)
	{
		return std::nullopt;
	}
}

inline std::string skillName(const CGHeroInstance * hero, std::string_view skillId)
{
	if(const auto definition = skillDefinition(hero, skillId); definition && !definition->name.empty())
		return definition->name;

	if(LIBRARY)
	{
		try
		{
			const int decoded = SecondarySkill::decode(std::string(skillId));
			if(decoded >= 0 && SecondarySkill::encode(decoded) == skillId)
				return SecondarySkill(decoded).toEntity(LIBRARY)->getNameTranslated();
		}
		catch(const std::exception &)
		{
			// Unknown legacy IDs are expected at this presentation boundary.
		}
	}

	return "Unknown";
}

inline std::string tierName(int requiredRank)
{
	switch(requiredRank)
	{
	case 1:
		return "Basic";
	case 2:
		return "Advanced";
	case 3:
		return "Expert";
	default:
		return {};
	}
}

inline std::string tierName(std::string_view prerequisite)
{
	try
	{
		return tierName(newHorizonsHeroes::perkRequiredRank(prerequisite));
	}
	catch(const std::exception &)
	{
		return {};
	}
}

/// Keep the owning Skill and tier first and concise, in the style of the
/// native Heroes III help popups.  The optional name is useful for a card/slot
/// whose title is otherwise only visible in the surrounding widget.
inline std::string format(const CGHeroInstance * hero, std::string_view skillId,
	std::string_view perkName, std::string_view tier, std::string_view description)
{
	std::string result = "Skill: " + skillName(hero, skillId);
	if(!tier.empty())
		result += "\nTier: " + std::string(tier);
	if(!perkName.empty())
		result += "\n\n" + std::string(perkName);
	if(!description.empty())
		result += "\n" + std::string(description);
	return result;
}
}
