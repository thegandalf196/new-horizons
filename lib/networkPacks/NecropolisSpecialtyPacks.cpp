/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NecropolisSpecialtyPacks.h"
#include "NetPackVisitor.h"
void RecordResidualLife::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRecordResidualLife(*this);
}
