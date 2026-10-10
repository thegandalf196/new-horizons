/*
 * GeneratedImageRecipes.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "GeneratedImageRecipes.h"
#include <array>

namespace generatedImageRecipes
{
namespace
{
constexpr std::array<CreaturePortrait, 3> recipes{{
	{"NH_academy_stoneGargoyle_icon_large.png", "CGARGO", EResType::ANIMATION,
		"NH_academy_creature_portrait_backdrop.png"},
	{"NH_academy_obsidianGargoyle_icon_large.png", "COGARG", EResType::ANIMATION,
		"NH_academy_creature_portrait_backdrop.png"},
	{"NH_academy_mageHolding_icon_large.png", "magi-vcmi-complete/battle/magi/idle/000.png", EResType::IMAGE,
		"NH_academy_creature_portrait_backdrop.png"}
}};
}
std::span<const CreaturePortrait> creaturePortraits()
{
	return recipes;
}
const CreaturePortrait * findCreaturePortrait(std::string_view image)
{
	// Use the same case/extension-normalized resource identity as ImagePath.
	const ResourcePath requested(std::string(image), EResType::IMAGE);
	for(const auto & recipe : recipes)
		if(requested == ResourcePath(std::string(recipe.output), EResType::IMAGE))
			return &recipe;
	return nullptr;
}
bool hasPhysicalDependencies(std::string_view image,
	const std::function<bool(const ResourcePath &)> & exists)
{
	const auto * recipe = findCreaturePortrait(image);
	if(!recipe || !exists(ResourcePath("Sprites/" + std::string(recipe->source), recipe->sourceType)))
		return false;
	for(const auto * prefix : {"Sprites/", "Data/", ""})
		if(exists(ResourcePath(prefix + std::string(recipe->backdrop), EResType::IMAGE)))
			return true;
	return false;
}
}
