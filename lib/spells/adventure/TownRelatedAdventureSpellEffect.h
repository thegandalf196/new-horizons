/*
 * TownRelatedAdventureSpellEffect.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "AdventureSpellEffect.h"

class CGTownInstance;
class CSpell;
class CGHeroInstance;
struct MapObjectSelectDialog;
class MetaString;

namespace spells
{
namespace adventure
{	
class DLL_LINKAGE TownRelatedAdventureSpellEffect : public IAdventureSpellEffect
{
protected:
	const CSpell * owner;
	bool allowTownSelection;
	bool skipOccupiedTowns;

	explicit TownRelatedAdventureSpellEffect(const CSpell * owner, bool allowTownSelection, bool skipOccupiedTowns);
	virtual std::vector<const CGTownInstance *> getPlayerTeamTowns(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const;
	const CGTownInstance * findNearestTown(const AdventureSpellCastParameters & parameters, const std::vector<const CGTownInstance *> & pool) const;

	virtual bool shouldOfferTownInDialog(const CGTownInstance * town) const;
	virtual void configureDialogTitleAndDescription(MetaString & title, MetaString & description) const = 0;
	virtual ESpellCastResult beginCastExtraChecks(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters, const std::vector<const CGTownInstance *> & towns) const;
	virtual ESpellCastResult onNoTownToSelect(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const;

public:
	virtual bool townSelectionAllowed(const CGHeroInstance * = nullptr) const { return allowTownSelection; }
	static const CGTownInstance * findNearestTown(const int3 & origin, const std::vector<const CGTownInstance *> & pool);
	ESpellCastResult beginCast(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters, const AdventureSpellMechanics & mechanics) const override;
};
}
}
