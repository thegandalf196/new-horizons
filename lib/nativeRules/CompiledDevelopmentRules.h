/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../entities/hero/NewHorizonsHeroRules.h"
#include <map>
#include "../entities/hero/NewHorizonsLearningSpecialties.h"
#include "../entities/hero/OrdinaryCreatureSpecialtyRegistry.h"
#include "PreparedPilotRules.h"
#include "../entities/hero/NewHorizonsLivingInferno.h"
#include "../entities/hero/NewHorizonsHerbalSurgeon.h"
#include "../entities/hero/NewHorizonsSwampSeer.h"
#include "../entities/hero/NewHorizonsReclaimer.h"
#include "../entities/hero/NewHorizonsLearningExchangeSpecialties.h"
#include "../entities/hero/NewHorizonsRevisedSpecialties.h"

#include <set>
#include <string_view>

namespace nativeRules
{
enum class DevelopmentContext { ResolvedHero, World };
/// Projection of admitted immutable data only. Rank, hero identity and RNG stay live.
class DLL_LINKAGE CompiledDevelopmentRules final
{
	struct GrowthRow
	{
		SecondarySkill skill;
		PrimarySkill attribute;
		std::array<int, 4> chance{};
	};
	bool enabled = false;
	int divisor = 1;
	int maximum = 0;
	int movementPerLevel = 0;
	int lighthouseDeparture = 0;
	bool defaultCreatureTable = false;
	bool capturedCreatureTarget = false;
	std::map<HeroTypeID, CreatureID> defaultCreatures;
	std::map<std::pair<HeroTypeID, SpellID>, int> chosenDuration;
	std::map<std::pair<HeroTypeID, SpellID>, int> chosenDiscount;
	bool weightedOffers = false;
	bool splitOffers = false;
	bool navigationStart = false;
	bool ancientOaths = false;
	std::optional<newHorizonsHeroes::PrimaryProfile> primary;
	std::map<HeroClassID, newHorizonsHeroes::PrimaryProfile> classes;
	std::vector<GrowthRow> growth;
	std::map<SecondarySkill, int> weights;
	std::map<std::string, int> namedWeights;
	std::set<SecondarySkill> excluded;
	std::set<std::string> knownSkills;
	std::map<HeroTypeID, const newHorizonsHeroes::detail::OrdinaryCreatureSpecialty *> ordinaryRows;
	std::set<std::string, std::less<>> ordinaryFields;
	std::map<HeroTypeID, CreatureID> ordinaryCreatures;
	std::map<HeroTypeID, int> ordinaryHeroValues;
	std::map<std::string, int, std::less<>> ordinaryValues;
	std::map<HeroTypeID, std::shared_ptr<const PreparedPilotCreature>> creatureMechanics;
	void compileSpecialties(const JsonNode &);
	void compileCombatPolicies(const JsonNode &);
	void compileSkillPolicies(const JsonNode &);
	std::map<FactionID, SecondarySkill> factions;
	std::map<FactionID, std::set<SecondarySkill>> factionAliases;
	std::set<SecondarySkill> uniqueSkills;
	std::map<SecondarySkill, std::optional<SecondarySkill>> rewardSkills;
public:
	static const CompiledDevelopmentRules & absent();
	explicit CompiledDevelopmentRules(const JsonNode &, DevelopmentContext = DevelopmentContext::ResolvedHero);
	bool active() const { return enabled; }
	int powerDivisor() const { return divisor; }
	int maximumPrimary() const { return maximum; }
	int lighthouseDeparturePercent() const { return lighthouseDeparture; }
	int64_t levelMovementBonus(uint32_t level) const { return level <= 1 ? 0 : static_cast<int64_t>(level - 1) * movementPerLevel; }
	int chosenDurationBonus(HeroTypeID, SpellID) const;
	int chosenManaDiscount(HeroTypeID, SpellID) const;
	const std::optional<newHorizonsHeroes::PrimaryProfile> & profile() const { return primary; }
	std::optional<newHorizonsHeroes::PrimaryProfile> classProfile(HeroClassID) const;
	bool usesSkillOfferWeights() const { return weightedOffers; }
	bool usesSplitSkillOffers() const { return splitOffers; }
	bool usesNavigationStartReplacement() const { return navigationStart; }
	std::optional<int> skillOfferWeight(SecondarySkill) const;
	std::optional<int> skillOfferWeight(const std::string &) const;
	const std::map<std::string, int> & offerWeights() const { return namedWeights; }
	bool isExcludedSkill(SecondarySkill skill) const { return excluded.contains(skill); }
	std::vector<newHorizonsHeroes::SkillGrowthChance> skillGrowthChances(
		const std::function<int(SecondarySkill)> &) const;
	// Resolved specialty policies contain IDs and scalars, never live hero state.
	std::optional<newHorizonsHeroes::CreatureLineSpecialtyRules> creatureLine;
	std::optional<newHorizonsHeroes::DamageSpellSpecialtyRules> damageSpell;
	std::optional<newHorizonsHeroes::NonDamageSpellSpecialtyRules> nonDamageSpell;
	std::optional<newHorizonsHeroes::SkillSpecialtyRules> skillSpecialty;
	std::optional<newHorizonsHeroes::DruidSpecialtyRules> druid;
	std::optional<newHorizonsHeroes::FortuneChildSpecialtyRules> fortuneChild;
	std::optional<newHorizonsHeroes::ArcanePreparationSpecialtyRules> arcanePreparation;
	std::optional<newHorizonsHeroes::BottomlessMindSpecialtyRules> bottomlessMind;
	std::optional<newHorizonsHeroes::MasterSpellwardSpecialtyRules> masterSpellward;
	std::optional<newHorizonsHeroes::IndividualSpecialtyRules> individual;
	std::optional<newHorizonsHeroes::ManaFurnaceRules> manaFurnace;
	std::optional<newHorizonsHeroes::FieldPhysicianRules> fieldPhysician;
	std::array<std::optional<newHorizonsHeroes::WarlocksFormulaRules>, 4> schoolSpellcraft;
	std::array<std::optional<newHorizonsHeroes::RecoverySpecialtyRules>, 3> recovery;
	std::array<std::optional<newHorizonsHeroes::LearningSpecialtyRules>, 2> learning;
	const newHorizonsHeroes::detail::OrdinaryCreatureSpecialty * ordinary(HeroTypeID, bool prepared = false) const;
	bool hasOrdinaryField(std::string_view key) const { return ordinaryFields.contains(key); }
	int ordinaryValue(std::string_view) const;
	int ordinaryValue(HeroTypeID) const;
	std::optional<CreatureID> ordinaryCreature(HeroTypeID) const;
	std::optional<CreatureID> defaultCreature(HeroTypeID) const;
	bool hasDefaultCreatureTable() const { return defaultCreatureTable; }
	bool hasCapturedCreatureTarget() const { return capturedCreatureTarget; }
	std::shared_ptr<const PreparedPilotCreature> preparedCreature(HeroTypeID) const;
	std::optional<bool> paralyzingGaze;
	std::optional<bool> serpentsEmbrace;
	std::optional<bool> forestAmbush;
	std::optional<bool> aerialSupremacy;
	std::optional<int> dreadCavalry;
	std::optional<newHorizonsHeroes::SpecialtyLevelScaling> levelScaling;
	std::optional<int> manaLeeches;
	std::optional<newHorizonsHeroes::LivingInfernoRules> livingInferno;
	std::optional<newHorizonsHeroes::HerbalSurgeonRules> herbalSurgeon;
	std::optional<newHorizonsHeroes::SwampSeerRules> swampSeer;
	std::optional<newHorizonsHeroes::ReclamationSpecialtyRules> reclamation;
	std::array<std::optional<newHorizonsHeroes::LearningExchangeSpecialtyRules>, 2> learningExchange;
	bool ghostUpgrade = false;
	bool genieSage = false;
	bool petrifyingBlood = false;
	std::optional<SecondarySkill> factionSkill(FactionID) const;
	bool isFactionSkill(SecondarySkill skill) const { return uniqueSkills.contains(skill); }
	bool isFactionSkillForFaction(FactionID, SecondarySkill) const;
	std::optional<SecondarySkill> normalizeRewardSkill(SecondarySkill) const;
	bool usesAncientOaths() const { return ancientOaths; }
	int skillSpecialtyCoreBonusPercent(SecondarySkill) const;
};
}
