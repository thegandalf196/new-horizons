/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "BattleSide.h"
#include "HeroCommand.h"
namespace battle
{
class Unit;
/// Pure owned window values, not capture/command authority. Full canonical
/// accepted collections are synchronous inputs; only SecondWind's two query
/// fields are retained privately. No policy/order-state export is provided.
class DLL_LINKAGE OwnedUnitOrderWindows final
{
	class Data;
	std::shared_ptr<const Data> data;
	explicit OwnedUnitOrderWindows(std::shared_ptr<const Data> values);
public:
	using Inputs = BattleSideArray<std::optional<std::vector<HeroOrderState>>>;
	~OwnedUnitOrderWindows();
	static std::shared_ptr<const OwnedUnitOrderWindows> fromOwnedInputs(const Inputs & inputs);
	std::shared_ptr<const OwnedUnitOrderWindows> withOrders(BattleSide side,
		const std::vector<HeroOrderState> & acceptedOrders) const;
	bool secondWindApplies(BattleSide side, const Unit & unit) const;
};
}
