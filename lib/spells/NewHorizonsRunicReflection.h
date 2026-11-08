/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <memory>
#include <span>
#include <vector>
class Bonus;
class CGHeroInstance;
namespace battle { class Unit; }
namespace spells { class Mechanics; namespace effects { class Effect; } }
namespace newHorizonsRunicReflection
{
inline constexpr auto POLICY_KEY = "runicReflectionPolicyVersion";
enum class Outcome { Apply, Resist, Reflect };
/// Partitions the ordinary resistance sample; it never draws another outcome.
DLL_LINKAGE Outcome outcome(int effectiveResistance, int nativeReflection, int acceptedSample,
	bool legalReflection);
DLL_LINKAGE int reflectionChance(int effectiveResistance, int nativeReflection);
DLL_LINKAGE int nativeChance(const battle::Unit & unit);
/// Read-only pool for an already mirrored view of the same captured cast.
/// Each native attested effect must legally accept the one redirected share.
DLL_LINKAGE std::vector<const battle::Unit *> legalRecipients(const spells::Mechanics & reflected,
	std::span<const spells::effects::Effect * const> effects, const battle::Unit * reflector = nullptr);
/// Cold hero-bonus preparation. Native creature resistance remains unchanged.
DLL_LINKAGE std::vector<std::shared_ptr<Bonus>> preparedBonuses(const CGHeroInstance & hero);
}
