/*
 * SpellSchoolBadge.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SpellSchoolBadge.h"
#include "SpellSchoolBadgeLayout.h"

#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../widgets/Images.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/filesystem/Filesystem.h"

namespace
{
bool medallionAvailable(const ImagePath & path)
{
	const auto * resources = CResourceHandler::get();
	return resources->existsResource(path.addPrefix("SPRITES/")) || resources->existsResource(path.addPrefix("DATA/"));
}

std::vector<ImagePath> medallionsOf(SpellID spell)
{
	std::vector<ImagePath> result;
	if(!GAME || !GAME->interface() || !GAME->interface()->cb)
		return result;
	for(const auto & school : GAME->interface()->cb->getSpellSchools(spell))
	{
		const std::string identifier = SpellSchool::encode(school.getNum());
		const auto key = spellSchoolBadge::schoolKey(identifier);
		// Original four-element schools and unknown modded schools are not marked.
		if(!key)
			return {};
		const auto path = ImagePath::builtin("NH_school_badge_" + std::string(*key));
		if(!medallionAvailable(path))
			return {};
		result.push_back(path);
	}
	if(result.size() > spellSchoolBadge::MAX_SCHOOLS)
		return {};
	return result;
}
}

std::shared_ptr<SpellSchoolBadge> SpellSchoolBadge::create(SpellID spell)
{
	const auto paths = medallionsOf(spell);
	return paths.empty() ? nullptr : std::make_shared<SpellSchoolBadge>(paths);
}

int SpellSchoolBadge::overhang(SpellID spell)
{
	return medallionsOf(spell).empty() ? 0 : spellSchoolBadge::OVERHANG;
}

SpellSchoolBadge::SpellSchoolBadge(const std::vector<ImagePath> & medallionPaths)
	: CIntObject(0)
{
	OBJECT_CONSTRUCTION;
	pos.w = spellSchoolBadge::SCROLL_WIDTH;
	pos.h = spellSchoolBadge::SCROLL_HEIGHT;
	const auto origins = spellSchoolBadge::medallionOrigins(medallionPaths.size());
	for(size_t index = 0; index < origins.size(); ++index)
		medallions.push_back(std::make_shared<CPicture>(medallionPaths[index], origins[index]));
}
