/*
 * PrimaryAndExperienceReward.h, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <vector>
class IGameEventCallback;
class CGHeroInstance;
struct Component;
namespace Rewardable
{
struct Reward;
void appendExperienceComponents(std::vector<Component> & comps, const Reward & reward,
	const CGHeroInstance * hero, std::optional<Obj> source);
void appendPrimaryStudyComponents(std::vector<Component> & comps, const Reward & reward,
	const CGHeroInstance * hero, std::optional<Obj> source);
/// One accepted primary stage and one resulting XP grant; simulation authority only.
void grantPrimaryAndExperience(IGameEventCallback & gameEvents, const Reward & reward,
	const CGHeroInstance & hero, std::optional<Obj> source);
}
