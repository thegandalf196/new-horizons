/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class CGHeroInstance;
class JsonNode;
class CBattleInfoCallback;
struct HeroOrderState;
namespace battle { class Unit; }

namespace newHorizonsPassiveDivineMandate
{
inline constexpr int FURY_CHANCE_PERCENT = 10;
inline constexpr const char * SKILL = "new-horizons:divineMandate";
enum class Perk : uint16_t
{
	SACRED_COMMAND = 1 << 0,
	KNIGHTLY_VALOR = 1 << 1,
	CHAPLAINS_RESERVE = 1 << 2,
	SHIELD_OF_FAITH = 1 << 3,
	RIGHTEOUS_DISCIPLINE = 1 << 4,
	UNWAVERING_STANDARD = 1 << 5,
	INSPIRING_LEADERSHIP = 1 << 6,
	CONSECRATED_ARMS = 1 << 7,
	MANDATE_OF_HEAVEN = 1 << 8,
	DIVINE_FURY = 1 << 9
};

/// Captured passive-policy inputs. Legacy paired captures never produce this
/// snapshot. Pure consumers share the same coefficients without querying live
/// installed defaults, current action allowances, or random state.
struct DLL_LINKAGE Policy
{
	uint8_t rank = 0;
	uint16_t perks = 0;
	bool has(Perk perk) const;
	void validate() const;
	bool operator==(const Policy &) const = default;
};

/// The independent per-combat usage counter is not a Hero Action allowance.
/// A rejected cast or an empty useful spell pool never advances this value.
struct DLL_LINKAGE Usage
{
	uint8_t completedCasts = 0;
	void validate() const;
	bool operator==(const Usage &) const = default;
};

struct DLL_LINKAGE OrderRiders
{
	int morale = 0;
	int luck = 0;
	int meleeDamagePercent = 0;
	int enemyAttackReductionPercent = 0;
	int extraRetaliations = 0;
	bool ignoreNegativeMorale = false;
	int meleeDefenseIgnorePercent = 0;
	bool fury = false;
	bool operator==(const OrderRiders &) const = default;
};

struct DLL_LINKAGE TargetChoice
{
	uint32_t aimUnitId = 0;
	std::vector<uint32_t> recipientUnitIds;
	bool operator==(const TargetChoice &) const = default;
};

/// Each row is one equally likely spell. Single targets within a row are
/// equally likely after recipient preference. A mass row has one native aim
/// affecting all its recipients and requires no additional random target draw.
struct DLL_LINKAGE SpellChoice
{
	SpellID spell;
	SpellID family;
	bool massive = false;
	std::vector<TargetChoice> targets;
	bool operator==(const SpellChoice &) const = default;
};

struct DLL_LINKAGE SelectedSpell
{
	SpellID spell;
	TargetChoice target;
	bool operator==(const SelectedSpell &) const = default;
};

DLL_LINKAGE int orderEfficiencyPercent(const Policy & policy);
DLL_LINKAGE bool enabled(const JsonNode & capturedPerkRules);
DLL_LINKAGE Policy capturePolicy(const CGHeroInstance * hero);
DLL_LINKAGE uint8_t activationLimit(const Policy & policy);
DLL_LINKAGE uint8_t remainingActivations(const Policy & policy, Usage used);
DLL_LINKAGE std::optional<Usage> afterAcceptedCast(const Policy & policy, Usage before);
DLL_LINKAGE int manaRecovery(const Policy & policy);
DLL_LINKAGE int inspiringSpeed(const Policy & policy);
DLL_LINKAGE OrderRiders orderRiders(const Policy & policy);
DLL_LINKAGE OrderRiders activeOrderRiders(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE OrderRiders activeOrderRiders(const CBattleInfoCallback & battle, const battle::Unit * unit,
	const std::vector<HeroOrderState> & orders);

/// Input rows have already passed native known/school/legality/usefulness and
/// permanent-restoration checks. This operation preserves their stable order.
DLL_LINKAGE std::vector<SpellChoice> finalizeSpellPool(std::vector<SpellChoice> choices,
	const std::vector<uint32_t> & orderRecipientUnitIds);
/// Exact bounded draw indices supplied by authoritative RNG; no modulo bias,
/// random consumption, battle cloning or target-value maximization here.
DLL_LINKAGE SelectedSpell selectSpell(const std::vector<SpellChoice> & pool,
	std::size_t spellIndex, std::size_t targetIndex);
DLL_LINKAGE bool furyEligible(const Policy & policy, bool underOrder,
	bool ordinaryPrimaryCreatureAttack, bool hostileSurvivingTarget, bool legalHolyWrathTarget);
}
