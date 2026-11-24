/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AcademySpecialtyPacks.h"
#include "NetPackVisitor.h"

void SetNewHorizonsSpellPractice::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsSpellPractice(*this);
}
