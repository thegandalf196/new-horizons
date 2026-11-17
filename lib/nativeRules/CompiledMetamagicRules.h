/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../spells/NewHorizonsMetamagicResolution.h"
#include "../entities/hero/CompiledPerkCatalogue.h"
#include <memory>
class CGHeroInstance;
class IBattleInfo;

namespace nativeRules
{
class CapturedPerkRules;
enum class MetamagicPerk : uint8_t
{
	ConservedFormula, ManaRecycling, DisruptiveCasting, PurgingFormula,
	SpellSculpting, HexWeaving, WidenSpell, Matterbreaker, MagicalMemory, ArcaneRupture, Count
};

/// Immutable definitions from ONE admitted perk owner. Rank, selections, RNG,
/// recipient provenance and actual payment/removal remain operation-local.
class DLL_LINKAGE CompiledMetamagicRules final
{
	std::optional<newHorizonsMetamagic::ResolutionPolicy> capturedPolicy;
	std::optional<SecondarySkill> capturedSkill;
	HeroTypeID halon = HeroTypeID::NONE;
	std::array<newHorizonsHeroes::PerkId, static_cast<size_t>(MetamagicPerk::Count)> perks;
	std::array<bool, static_cast<size_t>(MetamagicPerk::Count)> available{};
public:
	explicit CompiledMetamagicRules(const CapturedPerkRules & owner);
	const newHorizonsMetamagic::ResolutionPolicy * policy() const;
	std::optional<SecondarySkill> skill() const { return capturedSkill; }
	bool isHalon(HeroTypeID casterType) const { return halon.hasValue() && casterType == halon; }
	int removalChance(int liveRank, HeroTypeID casterType) const;
	const newHorizonsHeroes::PerkId & perk(MetamagicPerk key) const;
	bool supports(MetamagicPerk key) const;
	int64_t discharge(int64_t actuallyRemovedMana) const;
	int64_t restoration(int64_t actuallyRemovedMana) const;
	int32_t recyclingRefund(bool currentlySelected, int64_t actuallyRemovedMana,
		int32_t actualPaid, int32_t alreadyRefunded, int32_t capacityRoom) const;
	int32_t conservedRefund(bool currentlySelected, int32_t actualPaid, int32_t alreadyRefunded,
		int32_t capacityRoom, bool validSingleTarget, bool completelyResisted) const;
};

/// A WorldMagic document owns this table; battle/forecast copies share that exact
/// immutable owner. Registry definitions supply identities/kinds, NEVER costs.
/// No source callback, global cache, mutable rank or JSON survives into queries.
struct MetamagicSpellIdentity
{
	SpellID family;
	bool positive = false;
	bool negative = false;
};

class DLL_LINKAGE CompiledMetamagicEnergy final
{
	std::vector<std::optional<newHorizonsMetamagic::EnergyDefinition>> definitions;
	std::vector<std::optional<MetamagicSpellIdentity>> identities;
	std::optional<SecondarySkill> markerSkill;
	size_t sourceResolutions = 0;
public:
	/// The owning CapturedRuleDocument has already validated this exact document.
	explicit CompiledMetamagicEnergy(const JsonNode & admittedMagic);
	/// Legacy/custom callback boundary only: detach and admit a complete snapshot.
	static std::shared_ptr<const CompiledMetamagicEnergy> compileRawBoundary(JsonNode capturedMagic);
	/// Raw descriptor compatibility, including non-fuel kind flags.
	const newHorizonsMetamagic::EnergyDefinition * definition(SpellID spell) const;
	/// Execution lookup: unknown/internal/nonmagical/free sources cannot fuel a cast.
	const newHorizonsMetamagic::EnergyDefinition * fuelDefinition(SpellID spell) const;
	const MetamagicSpellIdentity * identity(SpellID spell) const;
	std::optional<SecondarySkill> memorySkill() const { return markerSkill; }
	size_t sourceResolutionCount() const { return sourceResolutions; }
	size_t registeredSlotCount() const { return definitions.size(); }
};

/// Trusted hero queries never pair one capture's policy with another's catalogue.
DLL_LINKAGE const CompiledMetamagicRules * metamagicRules(const CGHeroInstance * hero);
DLL_LINKAGE int metamagicSkillRank(const CGHeroInstance * hero);
DLL_LINKAGE bool hasMetamagicPerk(const CGHeroInstance * hero, MetamagicPerk perk);
/// One handle per cast/evaluation; only explicit legacy/custom boundaries compile.
DLL_LINKAGE std::shared_ptr<const CompiledMetamagicEnergy> captureMetamagicEnergy(const IBattleInfo & battle);
}
