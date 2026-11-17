/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include <memory>
namespace battle
{
class CUnitState;
/// Explicit owning native acquire capability. An implementation must retain all
/// descriptor/query/environment dependencies and return its actual private
/// receiver. Refusal throws; callers never fall back to a borrowed clone.
class DLL_LINKAGE OwnedUnitStateLoanSource
{
public:
	virtual ~OwnedUnitStateLoanSource() = default;
	virtual std::shared_ptr<CUnitState> acquireOwnedState(const CUnitState & source) const = 0;
};
}
