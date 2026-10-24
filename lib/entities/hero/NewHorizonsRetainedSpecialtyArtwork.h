/*
 * NewHorizonsRetainedSpecialtyArtwork.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <optional>
#include <string>
class CHero;
class CGHeroInstance;
class JsonNode;
namespace newHorizonsHeroes::detail
{
/// Read-only artwork for retained specialties after converted providers fall through.
/// Caller must enforce the matching captured prototype/development/magic context.
std::optional<std::string> resolveRetainedSpecialtyArtwork(const CHero & hero,
	const JsonNode & magic, const CGHeroInstance * capturedHero);
}
