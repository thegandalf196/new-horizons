/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../spells/NewHorizonsMetamagicResolution.h"
#include "../spells/NewHorizonsMetamagicDoubleCast.h"
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
	SpellSculpting, HexWeaving, WidenSpell, Matterbreaker, MagicalMemory, ArcaneRupture,
	UnravelingMagic, ReconstitutingMagic, WidenSpells, Levitation, ArcaneBallistics, ArcaneCascade, EmpoweredSpells, Count
};
/// Metamagic 6 (Double Cast, captured policy 7). Separate from MetamagicPerk so
/// retired roster keys and their meanings are never reused.
enum class DoubleCastPerk : uint8_t
{
	ArcaneWard, ManaSiphon, DeepReserves, FormulaReserve,
	ArcaneCycle, Afterglow, Crescendo, CleansingWeave,
	GrandWeaving, Spellstorm, Count
};
/// One bit per DoubleCastPerk the hero currently has active.
using DoubleCastPerkSet = uint16_t;
constexpr DoubleCastPerkSet doubleCastPerkBit(DoubleCastPerk perk)
{
	return static_cast<DoubleCastPerkSet>(1u << static_cast<unsigned>(perk));
}

/// Immutable definitions from ONE admitted perk owner. Rank, selections, RNG,
/// recipient provenance and actual payment/removal remain operation-local.
class DLL_LINKAGE CompiledMetamagicRules final
{
	std::optional<newHorizonsMetamagic::ResolutionPolicy> capturedPolicy;
	std::optional<newHorizonsMetamagic::DoubleCastPolicy> capturedDoubleCast;
	std::optional<SecondarySkill> capturedSkill;
	HeroTypeID halon = HeroTypeID::NONE;
	std::array<newHorizonsHeroes::PerkId, static_cast<size_t>(MetamagicPerk::Count)> perks;
	std::array<bool, static_cast<size_t>(MetamagicPerk::Count)> available{};
	std::array<newHorizonsHeroes::PerkId, static_cast<size_t>(DoubleCastPerk::Count)> doubleCastPerkIds;
	std::array<bool, static_cast<size_t>(DoubleCastPerk::Count)> doubleCastAvailable{};
	bool active(DoubleCastPerkSet heroPerks, DoubleCastPerk key) const;
public:
	explicit CompiledMetamagicRules(const CapturedPerkRules & owner);
	const newHorizonsMetamagic::ResolutionPolicy * policy() const;
	std::optional<SecondarySkill> skill() const { return capturedSkill; }
	bool isHalon(HeroTypeID casterType) const { return halon.hasValue() && casterType == halon; }
	int removalChance(int liveRank, HeroTypeID casterType) const;
	int overflowPercent(int metamagicRank) const;
	int friendlyReductionPercent(int rank) const;
	int debuffRetryPercent(int rank) const;
	const newHorizonsHeroes::PerkId & perk(MetamagicPerk key) const;
	bool supports(MetamagicPerk key) const;
	int64_t discharge(int64_t actuallyRemovedMana) const;
	int64_t restoration(int64_t actuallyRemovedMana) const;
	int32_t recyclingRefund(bool currentlySelected, int64_t actuallyRemovedMana,
		int32_t actualPaid, int32_t alreadyRefunded, int32_t capacityRoom) const;
	int32_t conservedRefund(bool currentlySelected, int32_t actualPaid, int32_t alreadyRefunded,
		int32_t capacityRoom, bool validSingleTarget, bool completelyResisted) const;

	/// Double Cast (policy 7). Every accessor returns 0 for other captured versions,
	/// and a perk value is 0 unless that perk is in the supplied set and admitted.
	const newHorizonsMetamagic::DoubleCastPolicy * doubleCastPolicy() const;
	const newHorizonsHeroes::PerkId & perk(DoubleCastPerk key) const;
	bool supports(DoubleCastPerk key) const;
	int maxUses(int metamagicRank, DoubleCastPerkSet heroPerks, bool halonCaster) const;
	/// Captured rule only; server validation of the follow-up spell is separate.
	bool followUpMustDiffer() const;
	int arcaneWardShield(int spellPower, DoubleCastPerkSet heroPerks) const;
	int manaSiphonMana(DoubleCastPerkSet heroPerks) const;
	int formulaReserveMana(DoubleCastPerkSet heroPerks) const;
	/// True at the start of every interval-th round (3, 6, 9...).
	bool arcaneCycleRestores(int round, DoubleCastPerkSet heroPerks) const;
	int afterglowAdditionalRounds(DoubleCastPerkSet heroPerks) const;
	int crescendoSpellPower(int spentUses, DoubleCastPerkSet heroPerks) const;
	int cleansingWeaveEffects(DoubleCastPerkSet heroPerks) const;
	/// Applies after the threshold-th completed double cast in this combat.
	int spellstormManaReductionPercent(int completedDoubleCasts, DoubleCastPerkSet heroPerks) const;
};

/// A WorldMagic document owns this table; battle/forecast copies share that exact
/// immutable owner. Registry definitions supply identities/kinds, NEVER costs.
/// No source callback, global cache, mutable rank or JSON survives into queries.
enum class MetamagicTargetShape : uint8_t { Single, Multiple, Unsupported };
struct MetamagicSpellIdentity
{
	SpellID family;
	bool positive = false;
	bool negative = false;
	std::array<MetamagicTargetShape, 4> effectCardinalityByLevel{};
	std::array<bool, 4> singleTargetByLevel{};
	std::array<bool, 4> debuffByLevel{};
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
/// The hero's active, admitted Double Cast perks under its own captured rules.
DLL_LINKAGE DoubleCastPerkSet doubleCastPerks(const CGHeroInstance * hero);
/// One handle per cast/evaluation; only explicit legacy/custom boundaries compile.
DLL_LINKAGE std::shared_ptr<const CompiledMetamagicEnergy> captureMetamagicEnergy(const IBattleInfo & battle);
}
