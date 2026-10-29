/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../../lib/CStack.h"
#include "../../../server/battles/GateDrawbridgeFootprint.h"
namespace
{
class GateGeometryUnit : public CStack
{
public:
	bool wide = false;
	BattleSide physicalSide = BattleSide::ATTACKER;
	bool doubleWide() const override { return wide; }
	BattleSide unitSide() const override { return physicalSide; }
};
}
TEST(GateDrawbridgeFootprint, MatchesLegacyPredicateForBothSidesWidthsAndMoats)
{
	GateGeometryUnit unit;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		for(const bool wide : {false, true})
			for(const bool moat : {false, true})
			{
				unit.physicalSide = side;
				unit.wide = wide;
				const auto oldGate = [moat](BattleHex hex)
				{
					return (moat && hex == BattleHex::GATE_BRIDGE)
						|| hex == BattleHex::GATE_OUTER || hex == BattleHex::GATE_INNER;
				};
				for(int id = -1; id <= GameConstants::BFIELD_SIZE; ++id)
				{
					const BattleHex hex(id);
					const auto other = unit.occupiedHex(hex);
					const bool expected = oldGate(hex)
						|| (wide && other.isValid() && oldGate(other));
					EXPECT_EQ(gateDrawbridgeFootprintContains(unit, hex, moat), expected);
				}
			}
}
