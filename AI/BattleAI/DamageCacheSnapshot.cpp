/* DamageCacheSnapshot.cpp, VCMI / New Horizons, GPL v2.0 or later. */
#include "StdInc.h"
#include "AttackPossibility.h"
#include <bit>

namespace
{
bool sameDamageBits(const std::unordered_map<uint32_t, std::unordered_map<uint32_t, float>> & left,
	const std::unordered_map<uint32_t, std::unordered_map<uint32_t, float>> & right)
{
	if(left.size() != right.size())
		return false;
	for(const auto & [actor, values] : left)
	{
		const auto other = right.find(actor);
		if(other == right.end() || values.size() != other->second.size())
			return false;
		for(const auto & [target, value] : values)
		{
			const auto entry = other->second.find(target);
			if(entry == other->second.end()
				|| std::bit_cast<uint32_t>(value) != std::bit_cast<uint32_t>(entry->second))
				return false;
		}
	}
	return true;
}
}

DamageCache::Snapshot DamageCache::captureSnapshot() const
{
	Snapshot result;
	for(const auto * layer = this; layer; layer = layer->parent)
	{
		result.layers.push_back(*layer);
		result.layers.back().parent = nullptr; // Own every layer; never retain stack-local parent addresses.
	}
	return result;
}

bool DamageCache::matchesSnapshot(const Snapshot & snapshot) const
{
	const auto * layer = this;
	for(const auto & saved : snapshot.layers)
	{
		if(!layer || !sameDamageBits(layer->damageCache, saved.damageCache)
			|| layer->obstacleDamage != saved.obstacleDamage
			|| layer->rangedMarkTargets != saved.rangedMarkTargets
			|| layer->evasiveShroudTargets != saved.evasiveShroudTargets
			|| layer->ambusherAttackers != saved.ambusherAttackers
			|| layer->shadowAssaultTargetSides != saved.shadowAssaultTargetSides
			|| layer->nightProwlerAttackers != saved.nightProwlerAttackers
			|| layer->frozenTargets != saved.frozenTargets
			|| layer->intrinsicMdrTargets != saved.intrinsicMdrTargets
			|| layer->intrinsicPdrTargets != saved.intrinsicPdrTargets)
			return false;
		layer = layer->parent;
	}
	return layer == nullptr;
}
