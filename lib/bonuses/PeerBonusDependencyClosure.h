/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusList.h"
#include <vector>
namespace ownedRecipientPeer
{
/// Pure selection over already-authorized RAW locally-arrived records, not a live collector.
/// Retain input payload/native interval through deep ownership capture; output aliases input.
/// Preserve every node, order and alias occurrence. No updater/limiter evaluation is performed.
DLL_LINKAGE std::vector<BonusList> dependencyClosure(const std::vector<BonusList> & local);
}
