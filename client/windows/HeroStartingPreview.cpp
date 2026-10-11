/*
 * HeroStartingPreview.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroStartingPreview.h"
#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/CMapService.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/artifact/CArtifact.h"

namespace
{
newHorizonsHeroes::StartingHeroOverrides authoredOverrides(const CGHeroInstance & hero)
{
	return newHorizonsHeroes::authoredStartingHeroOverrides(hero);
}
}

HeroStartingPreview heroStartingPreview(HeroTypeID id, const CMapInfo * selectedMap,
	PlayerColor owner, const CGHeroInstance * capturedHero)
{
	const auto & prototype = *id.toHeroType();
	auto context = newHorizonsHeroes::startingHeroContext(*LIBRARY->engineSettings());
	newHorizonsHeroes::StartingHeroOverrides overrides;
	const IGameSettings * effectiveSettings = LIBRARY->engineSettings();
	std::unique_ptr<CMap> map;
	HeroStartingPreview result;
	if(selectedMap && !selectedMap->isRandomMap && !selectedMap->campaign
		&& !selectedMap->scenarioOptionsOfSave && !selectedMap->fileURI.empty())
	{
		// Preparation only: no initHero, simulation, random sampling or world.
		// Full map parsing is necessary: CMapHeader does not carry GameSettings.
		try
		{
			map = CMapService().loadMap(ResourcePath(selectedMap->fileURI, EResType::MAP), nullptr);
			effectiveSettings = &map->getSettings();
			context = newHorizonsHeroes::startingHeroContext(*effectiveSettings);
			result.defaultProfile = false;
			for(const auto & object : map->objects)
				if(const auto * hero = dynamic_cast<const CGHeroInstance *>(object.get());
					hero && hero->getHeroTypeID() == id && hero->getOwner() == owner)
				{
					overrides = authoredOverrides(*hero);
					break;
				}
		}
		catch(const std::exception & error)
		{
			// Unavailable map context is explicitly a DEFAULT preview, not an
			// assertion that installation policy equals the selected map.
			logGlobal->warn("Cannot prepare selected-map starting hero preview: %s", error.what());
			context = newHorizonsHeroes::startingHeroContext(*LIBRARY->engineSettings());
			effectiveSettings = LIBRARY->engineSettings();
			overrides = {};
			result.defaultProfile = true;
		}
	}
	else if(!selectedMap && GAME->interface() && GAME->interface()->cb)
	{
		const auto & callback = *GAME->interface()->cb;
		const auto bounds = newHorizonsHeroes::startingHeroContext(*LIBRARY->engineSettings());
		context = {callback.getHeroDevelopmentRules(), callback.getHeroCapabilityRules(),
			callback.getHeroPerkRules(), callback.getMagicRules(), false,
			bounds.minimumPrimary, bounds.maximumPrimary};
		effectiveSettings = &callback.getSettings();
	}
	if(capturedHero)
	{
		// A saved hero supplies saved policy, not newly installed defaults.
		// These are START values under that policy, never current equipment,
		// level, learned book or mutated army presented as a starting package.
		context.development = capturedHero->getPrimaryGrowthRules();
		context.capabilities = capturedHero->getCapabilityRules();
		context.perks = capturedHero->getPerkState().rules;
		context.magic = capturedHero->getMagicRules();
		context.resolved = true;
	}
	result.values = newHorizonsHeroes::projectStartingHero(prototype, context, overrides);
	std::optional<CreatureID> specialtyTarget;
	if(capturedHero)
		specialtyTarget = capturedHero->getCreatureLineSpecialtyTarget();
	else if(!context.resolved && !overrides.skills)
		specialtyTarget = newHorizonsHeroes::defaultCreatureLineTarget(context.development, id);
	result.specialty = heroSpecialtyPresentation(prototype, context.development, specialtyTarget);
	for(const auto & selection : result.values.perks)
		for(const auto & row : context.perks["skills"][selection.skillId]["perks"].Vector())
			if(row["id"].String() == selection.perkId)
				result.perkNames[selection.perkId] = row["name"].String();
	result.stackChances = effectiveSettings->getVector(EGameSettings::HEROES_STARTING_STACKS_CHANCES);
	if(overrides.army)
		result.stackChances.assign(overrides.army->size(), 100);
	return result;
}

std::string startingSkillPerkNames(const HeroStartingPreview & preview, SecondarySkill skill)
{
	std::string result;
	const auto canonical = SecondarySkill::encode(skill.getNum());
	for(const auto & selection : preview.values.perks)
		if(selection.skillId == canonical)
		{
			if(!result.empty())
				result += ", ";
			const auto found = preview.perkNames.find(selection.perkId);
			result += found == preview.perkNames.end() || found->second.empty()
				? selection.perkId : found->second;
		}
	return result;
}

