/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/gameState/ReplayLog.h"
#include <array>
namespace
{
using Version = ESerializationVersion;
constexpr std::array<Version, 7> epochs{{static_cast<Version>(1144), static_cast<Version>(1145),
	static_cast<Version>(1146), static_cast<Version>(1147), static_cast<Version>(1148),
	static_cast<Version>(1149), Version::CURRENT}};
Bonus initiative(int value)
{
	Bonus result(BonusDuration::PERMANENT, BonusType::STACKS_INITIATIVE_FLAT,
		BonusSource::HERO_SPECIAL, value, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:torosar"))));
	result.stacking = "new-horizons:ordinary-creature-specialty:core:torosar";
	result.valType = BonusValueType::BASE_NUMBER;
	auto family = std::make_shared<CCreatureTypeLimiter>();
	family->creatureID = CreatureID(CreatureID::decode("core:ballista"));
	family->includeUpgrades = true;
	result.limiter = family;
	return result;
}
class MachineGenericBonusSerializationTest : public BattleTestFixture
{
protected:
	template<class T> std::vector<std::byte> bytes(T & source, Version epoch)
	{
		CMemorySerializer wire;
		wire.oser.version = epoch;
		wire.oser & source;
		return wire.extractBuffer();
	}
};
TEST_F(MachineGenericBonusSerializationTest, BareInitiativeKeepsExistingSemanticsAndExactHistoricalBytes)
{
	startGame();
	for(int value : {0, 2})
	{
		auto source = initiative(value);
		const auto current = bytes(source, Version::CURRENT);
		for(const auto epoch : epochs)
		{
			EXPECT_EQ(bytes(source, epoch), current);
			CMemorySerializer wire(current);
			wire.iser.version = epoch;
			Bonus restored;
			ASSERT_NO_THROW(wire.iser & restored);
			EXPECT_EQ(restored.toJsonNode(), source.toJsonNode());
		}
	}
}
TEST_F(MachineGenericBonusSerializationTest, ListAndConcreteArmyNodeRetainGenericInitiativeAtEveryPriorEpoch)
{
	startGame();
	for(const auto epoch : epochs)
	{
		BonusList list;
		list.push_back(std::make_shared<Bonus>(initiative(2)));
		CMemorySerializer listWire(bytes(list, epoch));
		listWire.iser.version = epoch;
		BonusList restored;
		ASSERT_NO_THROW(listWire.iser & restored);
		EXPECT_EQ(restored.toJsonNode(), list.toJsonNode());
		CBonusSystemNode node(BonusNodeType::ARMY), decoded(BonusNodeType::ARMY);
		node.addNewBonus(std::make_shared<Bonus>(initiative(0)));
		CMemorySerializer nodeWire(bytes(node, epoch));
		nodeWire.iser.version = epoch;
		ASSERT_NO_THROW(nodeWire.iser & decoded);
		EXPECT_EQ(decoded.getExportedBonusList().toJsonNode(), node.getExportedBonusList().toJsonNode());
	}
}
TEST_F(MachineGenericBonusSerializationTest, EffectPacketsAndDeclaredReplayDoNotInventAReservedInitiativeMarker)
{
	startGame();
	for(const auto epoch : epochs)
		for(size_t route = 0; route < 3; ++route)
		{
			SetStackEffect source;
			source.battleID = BattleID(0);
			auto * changes = route == 0 ? &source.toAdd : route == 1 ? &source.toUpdate : &source.toRemove;
			changes->push_back({1, {initiative(2)}});
			CMemorySerializer wire(bytes(source, epoch));
			wire.iser.version = epoch;
			SetStackEffect restored;
			ASSERT_NO_THROW(wire.iser & restored);
			const auto & decoded = route == 0 ? restored.toAdd : route == 1 ? restored.toUpdate : restored.toRemove;
			ASSERT_EQ(decoded.size(), 1u);
			ASSERT_EQ(decoded.front().second.size(), 1u);
			EXPECT_EQ(decoded.front().second.front().toJsonNode(), changes->front().second.front().toJsonNode());
			auto payload = ReplayPackSerializer::write(source);
			payload.version = epoch; // These packet fields are identical throughout1144..1150.
			auto replay = ReplayPackSerializer::read(payload, gameState().get());
			auto * effect = dynamic_cast<SetStackEffect *>(replay.get());
			ASSERT_NE(effect, nullptr);
			EXPECT_EQ(bytes(*effect, epoch), bytes(source, epoch));
		}
}
}
