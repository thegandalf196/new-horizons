/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../battle/UnitShieldState.h"
#include <optional>
namespace spells
{
class Mechanics;
/// Caller-owned quota: pure forecasts never consume the live cast ledger.
DLL_LINKAGE battle::DamageContext previewSpellDamageContext(const Mechanics &, const battle::Unit &,
	std::optional<battle::VowCasualtyQuota> & storage);
}
