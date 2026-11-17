/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include <algorithm>
#include <set>
#include <typeinfo>

namespace forecast::ammoCartAdmission
{
inline bool isInertIntrinsic(const Bonus & value)
{
	return value.type == BonusType::NOT_ACTIVE && value.val == 0
		&& value.source == BonusSource::CREATURE_ABILITY && value.sid == BonusSourceID(CreatureID(CreatureID::AMMO_CART))
		&& value.duration == BonusDuration::PERMANENT && value.turnsRemain == 0
		&& value.subtype == BonusSubtypeID() && value.valType == BonusValueType::ADDITIVE_VALUE
		&& value.targetSourceType == BonusSource::OTHER && value.effectRange == BonusLimitEffect::NO_LIMIT
		&& value.stacking.empty() && !value.parameters && !value.limiter && !value.propagator
		&& !value.updater && !value.propagationUpdater && !value.hasStatusMetadata()
		&& !value.hidden && value.bonusOwner == PlayerColor::CANNOT_DETERMINE && !value.appliedByEnemy
		&& value.spellCasterOwner == PlayerColor::CANNOT_DETERMINE
		// Content loading resolves the Bonus type after the creature loader's
		// fromInnateBonus call. The authored passive row can have no descriptor;
		// an explicit descriptor must still be exactly its passive identity.
		&& (!value.abilityDescriptor
			|| creatureAbility::sameDescriptor(value.abilityDescriptor, creatureAbility::fromInnateBonus(value)));
}

/// Check pre-limitation rows. A dormant foreign source must not borrow the
/// intrinsic exception merely because its recipient is the canonical Cart.
inline bool hasOnlyIntrinsicSource(const CStack & native, const CCreature & prototype, const Bonus * intrinsic)
{
	std::vector<const CBonusSystemNode *> pending{&native};
	std::set<const CBonusSystemNode *> seen;
	size_t rows = 0, edges = 0;
	const auto & graph = liveBonusGraph();
	while(!pending.empty())
	{
		const auto * node = pending.back(); pending.pop_back();
		if(!node) return false;
		if(!seen.insert(node).second) continue;
		if(seen.size() > 1024) return false;
		const auto & local = graph.local(*node);
		rows += local.size();
		if(rows > 8192) return false;
		for(const auto & value : local)
			if(!value || (value->type == BonusType::NOT_ACTIVE
				&& (node != &prototype || value.get() != intrinsic))) return false;
		const auto & parents = graph.parents(*node);
		edges += parents.size();
		if(edges > 4096) return false;
		pending.insert(pending.end(), parents.begin(), parents.end());
	}
	return seen.contains(&prototype);
}

inline const Bonus * intrinsic(const StackWithBonuses & unit)
{
	if(unit.creatureId() != CreatureID::AMMO_CART || !unit.isAmmoCart()) return nullptr;
	const auto * prototype = unit.unitType();
	if(!prototype || typeid(*prototype) != typeid(CCreature)) return nullptr;
	NativeBonusGraphAccess interval;
	const Bonus * result = nullptr;
	for(const auto & value : prototype->getExportedBonusList())
		if(value && value->type == BonusType::NOT_ACTIVE)
		{
			if(result || !isInertIntrinsic(*value)) return nullptr;
			result = value.get();
		}
	if(!result) return nullptr;
	std::vector<recipientBonusOverlay::Layer> layers;
	const CStack * native = nullptr;
	if(!unit.captureRecipientOverlayLayers(layers, native) || !native) return nullptr;
	for(const auto & layer : layers)
	{
		const auto forbidden = [](const auto & values)
		{ return std::ranges::any_of(values, [](const auto & value) { return !value || value->type == BonusType::NOT_ACTIVE; }); };
		if(forbidden(layer.added) || forbidden(layer.updated) || forbidden(layer.removed)) return nullptr;
		if(layer.stackedSnapshot && std::ranges::any_of(*layer.stackedSnapshot,
			[](const Bonus & value) { return value.type == BonusType::NOT_ACTIVE; })) return nullptr;
		if(layer.unstackedSnapshot && forbidden(*layer.unstackedSnapshot)) return nullptr;
	}
	return hasOnlyIntrinsicSource(*native, *prototype, result) ? result : nullptr;
}
}
