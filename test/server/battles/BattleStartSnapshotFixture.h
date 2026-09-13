/*
 * BattleStartSnapshotFixture.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace battleStartFixture
{
// BattleStart transports detached descriptors, not live CUnitState. Never detach
// the authoritative graph or weaken CStack::serialize's independence assertion.
class DescriptorWriter
{
	BinarySerializer & encoder;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = true;

	explicit DescriptorWriter(BinarySerializer & encoder) : encoder(encoder) {}

	bool hasFeature(Version version) const { return encoder.hasFeature(version); }

	template<typename T> DescriptorWriter & operator&(T & value)
	{
		encoder & value;
		return *this;
	}

	DescriptorWriter & operator&(std::vector<std::unique_ptr<CStack>> & units)
	{
		std::vector<std::unique_ptr<CStack>> descriptors;
		for(const auto & unit : units)
		{
			CStackBasicDescriptor base(unit->unitType()->getId(), unit->unitBaseAmount());
			auto copy = std::make_unique<CStack>(&base, unit->unitOwner(), static_cast<int>(unit->unitId()),
				unit->unitSide(), unit->unitSlot());
			copy->initialPosition = unit->initialPosition;
			for(const auto & bonus : unit->getExportedBonusList())
				copy->addNewBonus(std::make_shared<Bonus>(*bonus));
			descriptors.push_back(std::move(copy));
		}
		encoder & descriptors;
		return *this;
	}
};

inline void writeDescriptors(BattleInfo & source, CMemorySerializer & wire)
{
	DescriptorWriter writer(wire.oser);
	source.serialize(writer);
}

inline std::unique_ptr<BattleInfo> snapshot(BattleInfo & source, IGameInfoCallback * context)
{
	CMemorySerializer wire;
	writeDescriptors(source, wire);
	wire.iser.cb = context;
	auto result = std::make_unique<BattleInfo>(context);
	wire.iser & *result;
	return result;
}
}
