/*
 * GeneratedImageRecipes.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "ResourcePath.h"
#include <functional>
#include <span>
#include <string_view>

namespace generatedImageRecipes
{
/// Physical inputs only: these recipes never use generated resources or aliases.
struct DLL_LINKAGE CreaturePortrait
{
	std::string_view output;
	std::string_view source;
	EResType sourceType;
	std::string_view backdrop;
};
DLL_LINKAGE std::span<const CreaturePortrait> creaturePortraits();
DLL_LINKAGE const CreaturePortrait * findCreaturePortrait(std::string_view image);
/// The caller supplies ordinary scope-aware physical resource presence checks.
/// Source type is exact; backdrop alternatives match the existing portrait compositor.
DLL_LINKAGE bool hasPhysicalDependencies(std::string_view image,
	const std::function<bool(const ResourcePath &)> & exists);
}
