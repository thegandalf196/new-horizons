/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CCreatureHandler.h"
CCreature::CCreature() : CCreature(false) {}
CCreature::CCreature(bool hypothetical)
	: CBonusSystemNode(BonusNodeType::CREATURE, hypothetical)
{
	fightValue = AIValue = growth = hordeGrowth = ammMin = ammMax = 0;
}
