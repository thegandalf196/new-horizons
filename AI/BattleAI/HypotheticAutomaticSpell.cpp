/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
void HypotheticBattle::setPassiveDivineMandateUses(BattleSide side, uint8_t uses)
{
	if(uses > 5 || uses < passiveDivineMandateUses.at(side))
		throw std::logic_error("Invalid passive Divine Mandate projection usage");
	passiveDivineMandateUses.at(side) = uses;
}
