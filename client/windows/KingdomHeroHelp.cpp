/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CKingdomInterface.h"
#include "HeroSpecialtyPresentation.h"
#include "NewHorizonsPerkHelp.h"

std::string InfoBoxHeroData::getDetails()
{
	if(type == HERO_SPECIAL)
		return heroSpecialtyPresentation(*hero).details;
	if(type == HERO_SECONDARY_SKILL && getValue() > 0)
		return newHorizonsPerkHelp::skillDetails(hero, SecondarySkill(getSubID()), static_cast<int>(getValue()));
	return {};
}
