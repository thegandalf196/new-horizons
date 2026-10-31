/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitOrderWindows.h"
#include "BattleActivationPredicate.h"
#include "SideInBattle.h"
#include <stdexcept>
namespace battle
{
class OwnedUnitOrderWindows::Data
{
public:
	// Outer absence is UNKNOWN; inner absence is a known absent window.
	BattleSideArray<std::optional<std::optional<HeroOrderState>>> windows;
	static std::optional<HeroOrderState> select(const std::vector<HeroOrderState> & orders)
	{
		SideInBattle::validateOrderCollection(orders); // Before selecting/discarding ANY fields.
		for(const auto & order : orders)
			if(order.command == HeroCommand::SECOND_WIND)
			{
				HeroOrderState window;
				window.secondWindActive = order.secondWindActive;
				window.primaryTargetUnitId = order.primaryTargetUnitId;
				return window; // Never serialized/validated as a complete issued Order.
			}
		return std::nullopt;
	}
};
OwnedUnitOrderWindows::OwnedUnitOrderWindows(std::shared_ptr<const Data> values) : data(std::move(values)) {}
OwnedUnitOrderWindows::~OwnedUnitOrderWindows() = default;
std::shared_ptr<const OwnedUnitOrderWindows> OwnedUnitOrderWindows::fromOwnedInputs(const Inputs & inputs)
{
	auto values = std::make_shared<Data>();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(inputs.at(side)) values->windows.at(side).emplace(Data::select(*inputs.at(side)));
	return std::shared_ptr<const OwnedUnitOrderWindows>(new OwnedUnitOrderWindows(std::move(values)));
}
std::shared_ptr<const OwnedUnitOrderWindows> OwnedUnitOrderWindows::withOrders(BattleSide side,
	const std::vector<HeroOrderState> & acceptedOrders) const
{
	(void)data->windows.at(side); // Exact side-domain validation, before input work.
	const auto selected = Data::select(acceptedOrders);
	auto values = std::make_shared<Data>(*data);
	values->windows.at(side).emplace(selected);
	return std::shared_ptr<const OwnedUnitOrderWindows>(new OwnedUnitOrderWindows(std::move(values)));
}
bool OwnedUnitOrderWindows::secondWindApplies(BattleSide side, const Unit & unit) const
{
	const auto & captured = data->windows.at(side);
	if(!captured) throw std::logic_error("Owned SecondWind window is unknown");
	return isSecondWindRecipient(*captured ? &**captured : nullptr, unit);
}
}
