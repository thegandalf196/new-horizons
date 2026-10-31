/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackFormSourceIdentity.h"
#include "../CStack.h"
#include "../bonuses/NativeBonusGraphAccess.h"
namespace battle
{
StackFormSourceIdentity stackFormSourceIdentity(const CStack & stack)
{
	NativeBonusGraphAccess graphAccess;
	return {stack.typeID, stack.formBonusSource};
}
}
