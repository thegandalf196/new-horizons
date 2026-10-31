/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "UnitCreatureResolver.h"
#include "../CCreatureHandler.h"
#include <map>
#include <memory>
#include <optional>
namespace battle
{
class DLL_LINKAGE OwnedUnitCreatureResolver final : public UnitCreatureResolver, boost::noncopyable
{
public:
	struct Creature
	{
		CreatureID id;
		FactionID faction;
		int32_t level = 0;
		bool doubleWide = false;
		int32_t icon = -1;
		ArtifactID warMachine;
		TResources cost;
		std::string key, pluralTextID, singularTextID;
		// Prototype facts only; absence stays unknown, including in old callers.
		std::optional<int32_t> baseHitPoints;
		std::optional<int32_t> aiValue;
	};
	/// Inputs are owned values, not a callback-admission certificate. Finish construction before loans.
	explicit OwnedUnitCreatureResolver(std::vector<Creature> inputs);
	~OwnedUnitCreatureResolver() override;
	const CCreature * creature(CreatureID id) const override;
	int32_t decode(const std::string & key) const override;
	std::string encode(int32_t id) const override;
private:
	class Descriptor;
	std::map<CreatureID, std::unique_ptr<Descriptor>> descriptors;
	std::map<std::string, CreatureID> identifiers;
};
}
