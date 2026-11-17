/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/BonusEnum.h"
#include <bitset>

struct Bonus;
class CBonusSystemNode;
namespace battle
{
/// Capability, never effective applicability. Limiters, positions and counters
/// still use the ordinary query. Removal may retain a harmless false positive.
/// Plain values are copied with their branch; no shared mutable query cache.
class DLL_LINKAGE MechanicsPresence
{
	static constexpr size_t count = static_cast<size_t>(BonusType::BUILTIN_BONUSES_COUNT);
	std::bitset<count> types;
	bool unknown = false;
public:
	void include(const Bonus & bonus) noexcept;
	void include(const MechanicsPresence & other) noexcept
	{
		types |= other.types;
		unknown |= other.unknown;
	}
	bool mayHave(BonusType type) const noexcept
	{
		const auto index = static_cast<size_t>(type);
		return unknown || index >= count || types[index];
	}
};
/// Called only by the existing native cache publication under its owner lock.
DLL_LINKAGE MechanicsPresence nativeMechanicsPresence(const CBonusSystemNode & node);
}
