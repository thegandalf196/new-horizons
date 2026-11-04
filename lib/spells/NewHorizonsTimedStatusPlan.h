/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <cstdint>
#include <optional>
#include "../bonuses/Bonus.h"
struct Bonus;
class CSpell;
namespace spells { class Mechanics; }
namespace battle { class Unit; }
namespace newHorizonsMagic
{
/// Read-only loaded-definition identity; unsupported/custom records have no exact plan.
DLL_LINKAGE std::optional<Bonus> configuredRoundBonusIdentity(const CSpell&, int32_t level, BonusType);
/// Source-local refresh only; inherited auras never become recipient spell records.
DLL_LINKAGE int32_t refreshedTimedStatusDuration(const spells::Mechanics&, const battle::Unit&,
	Bonus incoming, int32_t rounds);
DLL_LINKAGE int64_t holyArmorReductionForRecipient(const spells::Mechanics&, int32_t configuredBase, const battle::Unit&);
DLL_LINKAGE int64_t heavenlyGaleReductionForRecipient(const spells::Mechanics&, const battle::Unit&);
DLL_LINKAGE int32_t entangleDurationForCast(const spells::Mechanics&);
DLL_LINKAGE int32_t misfortuneDurationForCast(const spells::Mechanics&);
DLL_LINKAGE int32_t misfortuneChanceForCast(const spells::Mechanics&);
DLL_LINKAGE int32_t chaosShieldReductionForCast(const spells::Mechanics&);
DLL_LINKAGE int32_t forgetfulnessDurationForCast(const spells::Mechanics&);
DLL_LINKAGE double misfortuneValueForRecipient(const spells::Mechanics&, const battle::Unit&, double ordinary);
/// Literal zero uses the already extended cast timer. Other round timers keep
/// their sign and receive Extend once; non-round timers remain unchanged.
DLL_LINKAGE int32_t configuredTimedDuration(int32_t configuredTurns, int32_t fallbackTurns,
	bool roundTimer, int32_t extendRounds);
DLL_LINKAGE int32_t configuredTimedDurationForCast(const spells::Mechanics&, int32_t configuredTurns,
	int32_t fallbackTurns, bool roundTimer);
}
