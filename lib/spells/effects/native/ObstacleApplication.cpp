/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ObstaclePlacement.h"
#include "../../ISpellMechanics.h"
#include <vstd/RNG.h>

namespace spells::effects::native
{
void ObstaclePlacement::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	const auto & battle = *mechanics->battle();
	const nativeEvents::Context context{*server, battle, nullptr, nullptr, parameters, {}};
	const auto count = quicksand(*mechanics) ? mechanics->getNewHorizonsQuicksandPatchCount() : parameters["patchCount"].Integer();
	std::vector<BattleHex> destinations;
	if(landMine(*mechanics))
	{
		if(static_cast<int>(target.size()) != mechanics->getNewHorizonsLandMinePatchCount()) return;
		for(const auto & destination : target) destinations.push_back(destination.hexValue);
	}
	else if(selectedQuicksand(*mechanics))
	{
		if(count <= 0 || !selectedValid(*mechanics, target, count)) return;
		for(const auto & destination : target) destinations.push_back(destination.hexValue);
	}
	else if(fireWall(*mechanics))
	{
		if(target.size() != 3) return;
		BattleHexArray footprint;
		for(const auto & destination : target) footprint.insert(destination.hexValue);
		structure::addObstacle(context, descriptor(*mechanics, target.front().hexValue, footprint));
		return;
	}
	else if(count > 0)
	{
		std::vector<BattleHex> available;
		if(mechanics->isMassive())
		{
			for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
			{
				const BattleHex hex(static_cast<si16>(index));
				if(structure::hexAvailable(battle, hex, true)) available.push_back(hex);
			}
		}
		else for(const auto & destination : target)
			if(structure::hexAvailable(battle, destination.hexValue, true)) available.push_back(destination.hexValue);
		for(size_t size = available.size(); size >= 2; --size)
			std::swap(available[size - 1], available[server->getRNG()->nextInt(1, static_cast<int>(size)) - 1]);
		const auto toPlace = std::min<int64_t>(count, available.size());
		destinations.assign(available.begin(), available.begin() + toPlace);
	}
	else for(const auto & destination : target) destinations.push_back(destination.hexValue);
	for(const auto hex : destinations)
	{
		BattleHexArray footprint;
		for(const auto & path : paths(options(mechanics->getCasterSide())["shape"])) footprint.insert(shape(hex, path));
		structure::addObstacle(context, descriptor(*mechanics, hex, footprint));
	}
}
}
