/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../GameConstants.h"
#include <cstdint>
#include <vector>

class IGameSettings;
class JsonNode;

namespace newHorizonsNeutralArmies::calibration
{
inline constexpr auto SETTING_KEY = "neutralLeadershipCalibration";

struct BudgetPoint
{
	int64_t original = 0;
	int64_t fieldable = 0;
};

struct FormationStack
{
	TQuantity count = 0;
	int unitValue = 0;
};

struct ReferenceStack
{
	int64_t available = 0;
	int unitValue = 0;
	int gold = 0;
};

/// Supply/Gold reference arithmetic; rejects unrepresentable inputs before mutation.
DLL_LINKAGE int64_t affordableValue(const std::vector<ReferenceStack> & army, int gold);

/// Frozen generation-only curve. Neither current heroes nor future growth enter.
class DLL_LINKAGE Rules
{
	std::vector<BudgetPoint> points;
public:
	explicit Rules(const JsonNode & snapshot);
	bool enabled() const { return !points.empty(); }
	int64_t budget(int64_t original) const;
	std::vector<TQuantity> counts(const std::vector<FormationStack> & formation) const;
};

DLL_LINKAGE JsonNode disabledSnapshot();

/// Fresh worlds with armyCommand capture disabled, without reading retired costs.
/// Historical definitions/captures retain their curve; Rules decodes without capture.
DLL_LINKAGE JsonNode capture(const IGameSettings & settings);
DLL_LINKAGE void validate(const JsonNode & value);
DLL_LINKAGE void validateOverrides(const JsonNode & settings, bool supported);
DLL_LINKAGE void validateSerialization(const IGameSettings & settings, bool supported);
}
