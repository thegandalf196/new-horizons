/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../battle/BattleEffectExchange.h"
#include <functional>
#include <optional>

namespace newHorizonsMetamagic
{
struct DLL_LINKAGE ResolutionPolicy
{
	std::array<int32_t, 4> chance;
	int32_t damagePerMana;
	int32_t restorationPerMana;
	int32_t halonChanceBonus;
};
DLL_LINKAGE std::optional<ResolutionPolicy> resolutionPolicy(const JsonNode & rules);
DLL_LINKAGE int removalChancePercent(const ResolutionPolicy &, int rank, bool halon);
DLL_LINKAGE int64_t energyHitPoints(int64_t removedMana, int32_t multiplier);
enum class ResolutionKind { OFFENSE, RESTORATION };

/// Metadata comes from the normalized ordinary spell, never the original payer.
struct DLL_LINKAGE EnergyDefinition
{
	SpellID family = SpellID::NONE;
	int32_t listedMana = 0;
	bool magical = false;
	bool positive = false;
	bool negative = false;
	bool commonHeroSpell = false;
};
using EnergyResolver = std::function<std::optional<EnergyDefinition>(SpellID)>;
DLL_LINKAGE std::optional<EnergyDefinition> energyDefinition(const JsonNode & rules, SpellID spell);

struct DLL_LINKAGE RemovalCandidate
{
	uint32_t recipient = 0;
	SpellID spell = SpellID::NONE;
	SpellID family = SpellID::NONE;
	PlayerColor caster = PlayerColor::CANNOT_DETERMINE;
	std::string statusIdentity;
	int32_t listedMana = 0;
	std::vector<Bonus> bonuses;
};

/// Immutable logical effects; RNG and state changes belong to simulation callers.
DLL_LINKAGE std::vector<RemovalCandidate> collectRemovalCandidates(uint32_t recipient,
	PlayerColor recipientOwner, const battle::BattleEffectSnapshot & snapshot,
	ResolutionKind kind, const EnergyResolver & resolver);
DLL_LINKAGE int unravelChancePercent(int rank);
DLL_LINKAGE int64_t dischargeHitPoints(int64_t removedMana);
DLL_LINKAGE int64_t restorationHitPoints(int64_t removedMana);
DLL_LINKAGE int32_t recyclingRefund(int64_t removedMana, int32_t actualPaid,
	int32_t alreadyRefunded, int32_t manaRoom);
DLL_LINKAGE int32_t conservedFormulaRefund(int32_t actualPaid, int32_t alreadyRefunded,
	int32_t manaRoom, bool validSingleTarget, bool completelyResisted);

/// One ephemeral ledger per original hero cast, shared by all recipients/hits.
/// Never create a new ledger for a triggered discharge or an ongoing damage tick.
/// No persistent battle/save state is introduced here.
class DLL_LINKAGE ResolutionLedger
{
public:
	explicit ResolutionLedger(bool originalHeroCast = true);
	/// roll receives the success percentage and is invoked once per logical effect.
	bool attempt(const RemovalCandidate & candidate, int rank,
		const std::function<bool(int)> & roll);
	bool attemptPercent(const RemovalCandidate & candidate, int chance,
		const std::function<bool(int)> & roll);
	/// Credit only an attempted successful bundle present in before and absent in after.
	/// The caller captures these around the validated authoritative removal operation.
	bool confirmRemoval(const RemovalCandidate & candidate,
		const battle::BattleEffectSnapshot & before, const battle::BattleEffectSnapshot & after);
	int64_t removedMana() const;
	int64_t removedMana(uint32_t recipient) const;
private:
	struct Attempt
	{
		RemovalCandidate candidate;
		bool successful = false;
		bool credited = false;
	};
	bool enabled;
	std::vector<Attempt> attempts;
};
}
