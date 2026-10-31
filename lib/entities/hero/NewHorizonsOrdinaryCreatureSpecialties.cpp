/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsOrdinaryCreatureSpecialties.h"
#include "OrdinaryCreatureSpecialtyRegistry.h"
#include "../../battle/NewHorizonsGnollHarriers.h"
#include "../../battle/NewHorizonsMachineSpecialties.h"
#include "../../battle/NewHorizonsOrdinaryRangedSpecialtyRules.h"
#include "../../battle/NewHorizonsRangefinder.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../bonuses/Bonus.h"
#include "../../bonuses/Limiters.h"
#include "../../bonuses/Propagators.h"
#include "../../bonuses/Updaters.h"
#include "../../CCreatureHandler.h"
#include "../../battle/BattleAttackInfo.h"
#include "../../battle/CBattleInfoCallback.h"
#include "../../battle/NewHorizonsCombatSkills.h"
#include "../../battle/Unit.h"
#include "../../battle/CUnitState.h"
namespace newHorizonsHeroes
{
namespace
{
constexpr std::string_view ordinaryPrefix = "new-horizons:ordinary-creature-specialty:";
const JsonNode * ordinaryPolicy(const JsonNode & development)
{
	if(!development.isStruct() || !development["additionalSpecialties"].isStruct()
		|| !development["additionalSpecialties"].Struct().contains("ordinaryCreatureSpecialties"))
		return nullptr;
	return &development["additionalSpecialties"]["ordinaryCreatureSpecialties"];
}
const detail::OrdinaryCreatureSpecialty * ordinaryIdentity(std::string_view hero, const JsonNode & development)
{
	if(!usesRules(development) || !ordinaryPolicy(development))
		return nullptr;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	const auto rows = detail::ordinaryCreatureSpecialtyRegistry();
	const auto found = std::ranges::find_if(rows,
		[hero](const auto & row) { return hero == row.hero; });
	return found == rows.end()
		|| !ordinaryPolicy(development)->Struct().contains(found->coefficient) ? nullptr : &*found;
}
int valueForIdentity(const CGHeroInstance * hero, std::string_view expectedIdentity)
{
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != expectedIdentity)
		return 0;
	const auto * row = ordinaryIdentity(expectedIdentity, hero->getPrimaryGrowthRules());
	return row ? static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[row->coefficient].Integer()) : 0;
}
}
void validateOrdinaryCreatureSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy)
		return;
	if(!supported)
		throw std::runtime_error("Captured ordinary creature specialties require their save capability");
	detail::fields(*policy, {"version", "closeQuartersDrill", "marshSharpshooter", "retaliationDamagePercent",
		"defendDefense", "maximumHealthPercent", "resistanceAuraPoints",
		"massedSpearsDamagePercent", "packPursuitDamagePercent", "bloodScentDamagePercent",
		"livingBastionDefense", "chillingPresenceReductionPercent", "boneFormationDefense",
		"stoneSentinelReductionPercent", "ogreWarcallerDamagePercent", "undertowRetaliationPercent",
		"wyvernsPreyDamagePercent", "mobPressureDamagePercent",
		"wingbeatInitiativePercent", "measuredVolleyMdrPenetrationPercent",
		"prismaticScreenReductionPercent", "evasiveFlightReductionPercent", "ridersExploitDamagePercent", "tidalRenewalHealingHP", "cruelVolleyDamagePercent", "rangefinderFullDamageDistance", "fieldReassemblyHealingHP", "fieldMaintenanceHealingHP", "ogreBulwarkRetaliations", "gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints", "heavyBatterySiegeTermBonusPercent", "shieldAndSwordPhysicalReductionPercent", "sepulchralGripSpeedReduction", "cyclopeanBreachIgnorePercent", "runicEnduranceReductionPercent",
		"scorchingImpactDamageReductionPercent", "cruelOverseerDefenseReduction", "lanceFormationDefensePerHex",
		"lanceFormationMaximumDefense", "stormWingsDefenseIgnorePercent", "stormWingsMinimumFlightDistance",
		"consecratedTargetDefenseIgnorePercent", "featheredFeintPhysicalReductionIgnorePercent", "demonicSolidarityReductionPercent",
		"ironHideReductionPercent", "graveDustHealingReductionBasisPoints", "cinderReprisalDamagePercent",
		"emberbrandDamagePercent", "toxicStingChancePercent", "gnollHarriersDamagePercent",
		"prismaticVanguardPhysicalDamagePercent", "siegeSapperCreatureDefenseIgnorePercent"});
	detail::require(detail::integer((*policy)["version"], 1, 1), "ordinary creature specialty version");
	for(const char * key : {"closeQuartersDrill", "marshSharpshooter"})
		detail::require(detail::integer((*policy)[key], 1, 1), "ordinary creature penalty waiver");
	for(const char * key : {"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"})
		detail::require(detail::integer((*policy)[key], 0, 100), "ordinary creature specialty coefficient");
	for(const char * key : {"massedSpearsDamagePercent", "packPursuitDamagePercent", "bloodScentDamagePercent",
		"livingBastionDefense", "chillingPresenceReductionPercent", "boneFormationDefense",
		"stoneSentinelReductionPercent", "ogreWarcallerDamagePercent", "undertowRetaliationPercent",
		"wyvernsPreyDamagePercent", "mobPressureDamagePercent",
		"wingbeatInitiativePercent", "measuredVolleyMdrPenetrationPercent",
		"prismaticScreenReductionPercent", "evasiveFlightReductionPercent", "ridersExploitDamagePercent", "tidalRenewalHealingHP", "cruelVolleyDamagePercent", "fieldReassemblyHealingHP", "fieldMaintenanceHealingHP", "cyclopeanBreachIgnorePercent", "runicEnduranceReductionPercent"})
		if(policy->Struct().contains(key))
			detail::require(detail::integer((*policy)[key], 0, 100), "conditional ordinary creature damage coefficient");
	if(policy->Struct().contains("rangefinderFullDamageDistance"))
		detail::require(detail::integer((*policy)["rangefinderFullDamageDistance"], 0, 12)
			&& ((*policy)["rangefinderFullDamageDistance"].Integer() == 0
				|| (*policy)["rangefinderFullDamageDistance"].Integer() == 12), "Rangefinder captured distance");
	if(policy->Struct().contains("ogreBulwarkRetaliations"))
		detail::require(detail::integer((*policy)["ogreBulwarkRetaliations"], 0, 1), "Ogre Bulwark retaliation count");
	const bool initiative = policy->Struct().contains("gunneryProfessorInitiativeFlat");
	const bool followUp = policy->Struct().contains("gunneryProfessorFollowUpBonusPoints");
	detail::require(initiative == followUp, "Gunnery Professor requires both captured coefficients");
	if(initiative)
	{
		detail::require(detail::integer((*policy)["gunneryProfessorInitiativeFlat"], 0, 100), "Gunnery Professor Initiative");
		detail::require(detail::integer((*policy)["gunneryProfessorFollowUpBonusPoints"], 0, 40), "Gunnery Professor follow-up points");
	}
	for(const char * key : {"shieldAndSwordPhysicalReductionPercent", "sepulchralGripSpeedReduction"})
		if(policy->Struct().contains(key))
			detail::require(detail::integer((*policy)[key], 0, 100), "ordinary melee specialty coefficient");
	if(policy->Struct().contains("heavyBatterySiegeTermBonusPercent"))
		detail::require(detail::integer((*policy)["heavyBatterySiegeTermBonusPercent"], 0, 100), "Heavy Battery Siege-term percentage");
	for(const char * key : {"scorchingImpactDamageReductionPercent", "cruelOverseerDefenseReduction", "lanceFormationDefensePerHex",
		"stormWingsDefenseIgnorePercent", "consecratedTargetDefenseIgnorePercent", "featheredFeintPhysicalReductionIgnorePercent",
		"demonicSolidarityReductionPercent", "ironHideReductionPercent", "cinderReprisalDamagePercent",
		"emberbrandDamagePercent", "toxicStingChancePercent", "gnollHarriersDamagePercent",
		"prismaticVanguardPhysicalDamagePercent", "siegeSapperCreatureDefenseIgnorePercent"})
		if(policy->Struct().contains(key))
			detail::require(detail::integer((*policy)[key], 0, 100), "extended ordinary specialty coefficient");
	detail::require(policy->Struct().contains("lanceFormationDefensePerHex") == policy->Struct().contains("lanceFormationMaximumDefense"),
		"Extended ordinary specialty requires both captured coefficients");
	if(policy->Struct().contains("lanceFormationMaximumDefense"))
		detail::require(detail::integer((*policy)["lanceFormationMaximumDefense"], 1, 100), "extended ordinary specialty companion");
	detail::require(policy->Struct().contains("stormWingsDefenseIgnorePercent") == policy->Struct().contains("stormWingsMinimumFlightDistance"),
		"Extended ordinary specialty requires both captured coefficients");
	if(policy->Struct().contains("stormWingsMinimumFlightDistance"))
		detail::require(detail::integer((*policy)["stormWingsMinimumFlightDistance"], 1, 187), "extended ordinary specialty companion");
	if(policy->Struct().contains("graveDustHealingReductionBasisPoints"))
		detail::require(detail::integer((*policy)["graveDustHealingReductionBasisPoints"], 0, 10000), "Grave Dust healing reduction");

}
void validateOrdinaryCreatureExtensionsSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	// First-six format1144 admits none of these keys, including attack-only
	// rows. Do not export a newer policy that its strict reader cannot load.
	for(const char * key : {"massedSpearsDamagePercent", "packPursuitDamagePercent", "bloodScentDamagePercent",
		"livingBastionDefense", "chillingPresenceReductionPercent", "boneFormationDefense",
		"stoneSentinelReductionPercent", "ogreWarcallerDamagePercent", "undertowRetaliationPercent",
		"wyvernsPreyDamagePercent", "mobPressureDamagePercent",
		"wingbeatInitiativePercent", "measuredVolleyMdrPenetrationPercent"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Extended ordinary creature policies require adjacency-format support");
}

void validateOrdinaryCreaturePolicyExtensionsSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"prismaticScreenReductionPercent", "evasiveFlightReductionPercent",
		"ridersExploitDamagePercent", "tidalRenewalHealingHP"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Captured ordinary creature policy extensions require format1146");
}

void validateOrdinaryRangedSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"cruelVolleyDamagePercent", "rangefinderFullDamageDistance"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Captured ordinary ranged specialties require format1148");
}

void validateOrdinaryMaintenanceSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"fieldReassemblyHealingHP", "fieldMaintenanceHealingHP", "ogreBulwarkRetaliations"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Captured ordinary maintenance specialties require format1149");
}

void validateOrdinaryMachineSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints", "heavyBatterySiegeTermBonusPercent"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Captured ordinary machine specialties require format1150");
}

void validateOrdinaryCombatSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"shieldAndSwordPhysicalReductionPercent", "sepulchralGripSpeedReduction",
		"cyclopeanBreachIgnorePercent", "runicEnduranceReductionPercent"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Captured ordinary combat specialties require format1151");
}

void validateExtendedOrdinarySpecialtiesSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported) return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"scorchingImpactDamageReductionPercent", "cruelOverseerDefenseReduction", "lanceFormationDefensePerHex",
		"lanceFormationMaximumDefense", "stormWingsDefenseIgnorePercent", "stormWingsMinimumFlightDistance",
		"consecratedTargetDefenseIgnorePercent", "featheredFeintPhysicalReductionIgnorePercent", "demonicSolidarityReductionPercent",
		"ironHideReductionPercent", "graveDustHealingReductionBasisPoints", "cinderReprisalDamagePercent",
		"emberbrandDamagePercent", "toxicStingChancePercent", "gnollHarriersDamagePercent",
		"prismaticVanguardPhysicalDamagePercent", "siegeSapperCreatureDefenseIgnorePercent"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Captured extended ordinary specialties require format1152");
}

const CGHeroInstance * ordinaryCreatureVisibleOwnerHero(const CBattleInfoCallback & battle, const battle::Unit & actor)
{
	const auto side = battle.playerToSide(battle.battleGetOwner(&actor));
	const auto policy = battle.battleGetVisibleHeroPolicy(side);
	return policy.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO ? policy.legacyHero() : nullptr;
}

bool hasOrdinaryCreatureSpecialty(const CGHeroInstance * hero)
{
	return hero && hero->getHeroType()
		&& ordinaryIdentity(hero->getHeroType()->getJsonKey(), hero->getPrimaryGrowthRules());
}

int ordinaryCreatureSpecialtyValue(const CBattleInfoCallback & battle, const battle::Unit & actor,
	std::string_view expectedIdentity)
{
	return valueForIdentity(ordinaryCreatureVisibleOwnerHero(battle, actor), expectedIdentity);
}
std::shared_ptr<Bonus> ordinaryCreatureSpecialtyBonus(const CGHeroInstance & hero)
{
	if(!hero.getHeroType())
		return nullptr;
	const auto * row = ordinaryIdentity(hero.getHeroType()->getJsonKey(), hero.getPrimaryGrowthRules());
	if(!row || row->type == BonusType::NONE)
		return nullptr;
	const auto creature = CreatureID(CreatureID::decode(row->creature));
	if(!creature.hasValue() || !creature.toCreature())
		throw std::runtime_error("Ordinary creature specialty family is not registered");
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, row->type, BonusSource::HERO_SPECIAL,
		static_cast<int>((*ordinaryPolicy(hero.getPrimaryGrowthRules()))[row->coefficient].Integer()),
		BonusSourceID(hero.getHeroTypeID()));
	bonus->valType = row->valueType;
	bonus->stacking = std::string(ordinaryPrefix) + row->hero;
	bonus->limiter = std::make_shared<CCreatureTypeLimiter>(*creature.toCreature(), true);
	if(std::string_view(row->hero) == "core:christian")
		newHorizonsRangefinder::configureEnvelope(*bonus, static_cast<int>((*ordinaryPolicy(hero.getPrimaryGrowthRules()))[row->coefficient].Integer()));
	if(std::string_view(row->hero) == "core:ryland" || std::string_view(row->hero) == "core:galthran"
		|| std::string_view(row->hero) == "core:charna" || std::string_view(row->hero) == "core:vey"
		|| std::string_view(row->hero) == "core:pasis")
	{
		auto adjacent = std::make_shared<UnitAdjacentLimiter>();
		adjacent->targetUnit = creature;
		adjacent->filter.includeUpgrades = true;
		adjacent->filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
		if(std::string_view(row->hero) == "core:galthran")
		{
			adjacent->filter.anyCreature = true;
			adjacent->targetUnit = CreatureID();
			adjacent->filter.nativeTrait = AdjacentUnitFilter::NativeTrait::UNDEAD;
			auto all = std::make_shared<AllOfLimiter>();
			all->add(bonus->limiter);
			all->add(adjacent);
			bonus->limiter = all;
		}
		else
			bonus->limiter = adjacent;
		if(std::string_view(row->hero) == "core:charna")
		{
			adjacent->filter.relationship = AdjacentUnitFilter::Relationship::OPPOSING_ARMY;
			bonus->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
			bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
			bonus->propagationUpdater = std::make_shared<OwnerUpdater>();
			auto all = std::make_shared<AllOfLimiter>();
			all->add(adjacent);
			all->add(std::make_shared<OppositeSideLimiter>());
			bonus->limiter = all;
		}
		else if(std::string_view(row->hero) == "core:pasis")
		{
			bonus->subtype = BonusSubtypeID(SpellSchool::ANY);
			auto all = std::make_shared<AllOfLimiter>();
			all->add(adjacent);
			// Existing native creature levels exclude level-zero machines/turrets.
			all->add(std::make_shared<CreatureLevelLimiter>(1));
			bonus->limiter = all;
		}
		else if(std::string_view(row->hero) == "core:vey")
		{
			bonus->subtype = BonusCustomSubtype::damageTypeMelee;
			bonus->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
		}
		else
			bonus->subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
	}
	return bonus;
}
int ordinaryCreatureRetaliationPercent(const CGHeroInstance * hero, const BattleAttackInfo & attack)
{
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != "core:dace"
		|| !hasOrdinaryCreatureSpecialty(hero) || !attack.retaliation || attack.shooting
		|| !attack.physicalDamage || attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0
		|| !attack.attacker || !attack.defender)
		return 0;
	const auto family = CreatureID(CreatureID::decode("core:minotaur"));
	const auto creature = attack.attacker->creatureId();
	if(creature != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
		return 0;
	return static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))["retaliationDamagePercent"].Integer());
}

int ordinaryCreatureAttackPercent(const CGHeroInstance * hero, const BattleAttackInfo & attack,
	bool victimHasOtherFriendlyNeighbor)
{
	if(!hero || !hero->getHeroType() || !attack.attacker || !attack.defender
		|| !attack.physicalDamage || attack.shooting || attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0)
		return 0;
	const auto * identity = ordinaryIdentity(hero->getHeroType()->getJsonKey(), hero->getPrimaryGrowthRules());
	if(!identity || (hero->getHeroType()->getJsonKey() != "core:shakti"
		&& hero->getHeroType()->getJsonKey() != "core:fiona" && hero->getHeroType()->getJsonKey() != "core:vokial"))
		return 0;
	const auto family = CreatureID(CreatureID::decode(identity->creature));
	const auto creature = attack.attacker->creatureId();
	if(creature != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
		return 0;
	bool qualifies = false;
	if(hero->getHeroType()->getJsonKey() == "core:shakti")
		qualifies = attack.attacker->getCount() > attack.defender->getCount();
	else if(hero->getHeroType()->getJsonKey() == "core:fiona")
		qualifies = victimHasOtherFriendlyNeighbor;
	else
		qualifies = attack.defender->alive() && attack.defender->isLiving()
			&& attack.defender->getPhantomInitialIntegrity() == 0
			&& attack.defender->getFirstHPleft() > 0
			&& static_cast<int64_t>(attack.defender->getFirstHPleft()) < attack.defender->getMaxHealth();
	return qualifies ? static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[identity->coefficient].Integer()) : 0;
}

int ordinaryCreatureDamageMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(const int harriers = newHorizonsGnollHarriers::damagePercent(battle, attack); harriers > 0)
		return 100 + harriers;
	if(attack.shooting)
	{
		if(!newHorizonsOrdinaryRangedSpecialtyRules::orcCasualtyShot(attack)
			|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender))
			return 100;
		const auto * hero = ordinaryCreatureVisibleOwnerHero(battle, *attack.attacker);
		if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != "core:jabarkas")
			return 100;
		const auto * row = ordinaryIdentity(hero->getHeroType()->getJsonKey(), hero->getPrimaryGrowthRules());
		return row ? 100 + static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[row->coefficient].Integer()) : 100;
	}
	if(!attack.attacker || !attack.defender || !attack.physicalDamage || attack.shooting
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender))
		return 100;
	const auto * hero = ordinaryCreatureVisibleOwnerHero(battle, *attack.attacker);
	if(!hero || !hero->getHeroType())
		return 100;
	const auto key = hero->getHeroType()->getJsonKey();
	const auto * identity = ordinaryIdentity(key, hero->getPrimaryGrowthRules());
	auto hasNeighbor = [&](const battle::Unit * center, const battle::Unit * allegiance, bool goblinsOnly)
	{
		for(const auto * neighbor : battle.battleAdjacentUnits(center))
			if(neighbor->unitId() != attack.attacker->unitId() && neighbor->alive() && !neighbor->isGhost()
				&& neighbor->unitSlot() != SlotID::WAR_MACHINES_SLOT
				&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(neighbor)
				&& battle.battleGetOwner(neighbor) == battle.battleGetOwner(allegiance)
				&& (!goblinsOnly || neighbor->creatureId() == CreatureID(CreatureID::decode("core:goblin"))
					|| neighbor->creatureId() == CreatureID(CreatureID::decode("core:hobgoblin"))))
				return true;
		return false;
	};
	if(identity && (key == "core:lacus" || key == "core:broghild" || key == "core:gretchin"
		|| key == "core:tyraxor"))
	{
		const auto family = CreatureID(CreatureID::decode(identity->creature));
		const auto creature = attack.attacker->creatureId();
		if(creature != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
			return 100;
		const auto * victimState = dynamic_cast<const battle::CUnitState *>(attack.defender);
		const bool qualifies = key == "core:tyraxor"
			? attack.cleaveDamagePercent == 0 && victimState && !victimState->counterAttacks.canUse()
			: key == "core:lacus"
			? attack.retaliation && hasNeighbor(attack.attacker, attack.attacker, false)
			: key == "core:broghild" ? !hasNeighbor(attack.defender, attack.defender, false)
			: hasNeighbor(attack.defender, attack.attacker, true);
		return 100 + (qualifies ? static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[identity->coefficient].Integer()) : 0);
	}
	return 100 + ordinaryCreatureRetaliationPercent(hero, attack)
		+ ordinaryCreatureAttackPercent(hero, attack, key == "core:fiona" && hasNeighbor(attack.defender, attack.attacker, false));
}

int ordinaryCreatureProtectionPercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(attack.physicalDamage && !attack.shooting && attack.retaliation && !attack.secondaryAttack
		&& !attack.bracePreemptive && attack.preemptiveDamagePercent == 0 && attack.cleaveDamagePercent == 0
		&& attack.attacker && attack.defender && attack.defender->alive() && !attack.defender->isGhost()
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		&& battle.battleGetOwner(attack.attacker) != battle.battleGetOwner(attack.defender))
	{
		const auto * hero = ordinaryCreatureVisibleOwnerHero(battle, *attack.defender);
		const auto * row = hero && hero->getHeroType()
			? ordinaryIdentity(hero->getHeroType()->getJsonKey(), hero->getPrimaryGrowthRules()) : nullptr;
		if(row && hero->getHeroType()->getJsonKey() == "core:korbac")
		{
			const auto family = CreatureID(CreatureID::decode(row->creature));
			const auto creature = attack.defender->creatureId();
			if(creature == family || family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
				return static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[row->coefficient].Integer());
		}
	}

	if(!attack.physicalDamage || !attack.shooting || !attack.defender || !attack.defender->alive()
		|| attack.defender->isGhost() || !attack.defender->defended())
		return 0;
	const auto * hero = battle.battleGetFightingHero(attack.defender->unitSide());
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != "core:piquedram")
		return 0;
	const auto * identity = ordinaryIdentity(hero->getHeroType()->getJsonKey(), hero->getPrimaryGrowthRules());
	const auto family = CreatureID(CreatureID::decode("core:stoneGargoyle"));
	const auto creature = attack.defender->creatureId();
	return identity && (creature == family || family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
		? static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[identity->coefficient].Integer()) : 0;
}

std::shared_ptr<Bonus> ordinaryCreatureStrikeBonus(const CGHeroInstance * hero, const BattleAttackInfo & attack)
{
	if(!hero || !hero->getHeroType() || !attack.attacker || !attack.defender || !attack.physicalDamage
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0)
		return nullptr;
	const auto key = hero->getHeroType()->getJsonKey();
	const auto * row = ordinaryIdentity(key, hero->getPrimaryGrowthRules());
	if(!row || (key != "core:aeris" && key != "core:theodorus")
		|| (key == "core:aeris" ? attack.shooting : !attack.shooting || attack.retaliation))
		return nullptr;
	const auto family = CreatureID(CreatureID::decode(row->creature));
	const auto creature = attack.attacker->creatureId();
	if(creature != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
		return nullptr;
	const int coefficient = static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[row->coefficient].Integer());
	if(coefficient == 0)
		return nullptr;
	const bool wingbeat = key == "core:aeris";
	auto bonus = std::make_shared<Bonus>(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION,
		wingbeat ? BonusType::STACKS_INITIATIVE : BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION,
		BonusSource::HERO_SPECIAL, wingbeat ? -coefficient : coefficient, BonusSourceID(hero->getHeroTypeID()));
	bonus->stacking = std::string(ordinaryPrefix) + row->hero;
	bonus->statusIdentity = wingbeat ? "wingbeat" : "measuredVolley";
	bonus->statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	bonus->appliedByEnemy = true;
	bonus->description = MetaString::createFromRawString(std::string(row->name) + ": "
		+ (wingbeat ? "Initiative -" : "Existing magical damage reduction reduced relatively by ")
		+ std::to_string(coefficient) + "%; until next activation. Cure and Dispel do not remove this effect.");
	return bonus;
}

std::shared_ptr<Bonus> ordinaryCreatureStrikeBonus(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender)
		return nullptr;
	return ordinaryCreatureStrikeBonus(ordinaryCreatureVisibleOwnerHero(battle, *attack.attacker), attack);
}

bool ordinaryCreatureDamageIsStateful(const CBattleInfoCallback & battle)
{
	// Fixed policy presence also protects cached pairs after effective-owner changes.
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battle.battleGetFightingHero(side);
		if(!hero || !hero->getHeroType() || (hero->getHeroType()->getJsonKey() != "core:tyraxor"
			&& hero->getHeroType()->getJsonKey() != "core:jabarkas"
			&& hero->getHeroType()->getJsonKey() != "core:christian"
			&& hero->getHeroType()->getJsonKey() != "core:gurnisson"
			&& hero->getHeroType()->getJsonKey() != "core:yog"
			&& hero->getHeroType()->getJsonKey() != "core:ufretin"
			&& hero->getHeroType()->getJsonKey() != "core:drakon"
			&& hero->getHeroType()->getJsonKey() != "core:synca"))
			continue;
		const auto * row = ordinaryIdentity(hero->getHeroType()->getJsonKey(), hero->getPrimaryGrowthRules());
		if(row && (*ordinaryPolicy(hero->getPrimaryGrowthRules()))[row->coefficient].Integer() > 0)
			return true;
	}
	return false;
}

int ordinaryCreatureRoundHealing(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	if(!unit.alive() || unit.isGhost() || unit.isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&unit))
		return 0;
	// Inherited regeneration is passive: native army affiliation survives mind control.
	const auto * hero = battle.battleGetFightingHero(unit.unitSide());
	const auto * row = hero && hero->getHeroType()
		? ordinaryIdentity(hero->getHeroType()->getJsonKey(), hero->getPrimaryGrowthRules()) : nullptr;
	if(!row || hero->getHeroType()->getJsonKey() != "core:kalt")
		return 0;
	const auto family = CreatureID(CreatureID::decode(row->creature));
	const auto creature = unit.creatureId();
	if(creature != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
		return 0;
	const auto wounds = std::max<int64_t>(0, static_cast<int64_t>(unit.getMaxHealth()) - unit.getFirstHPleft());
	return static_cast<int>(std::min(wounds, (*ordinaryPolicy(hero->getPrimaryGrowthRules()))[row->coefficient].Integer()));
}

bool resolveOrdinaryCreatureSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	SpecialtyPresentation & result)
{
	const auto * row = ordinaryIdentity(hero.getJsonKey(), development);
	if(!row)
		return false;
	result.name = row->name;
	result.artwork = row->artwork;
	result.creature = CreatureID(CreatureID::decode(row->creature));
	result.description = row->description;
	const auto placeholder = result.description.find("%VALUE%");
	if(placeholder != std::string::npos)
	{
		const auto captured = (*ordinaryPolicy(development))[row->coefficient].Integer();
		const auto displayed = hero.getJsonKey() == "core:christian" && captured == 0 ? 10 : captured;
		result.description.replace(placeholder, 7, std::to_string(displayed));
	}
	if(hero.getJsonKey() == "core:torosar")
		result.description += " If Master Gunner is legitimately learned, its follow-up retains "
			+ std::to_string(newHorizonsMachineSpecialties::MASTER_GUNNER_ORDINARY_FOLLOW_UP_DAMAGE_PERCENT
				+ (*ordinaryPolicy(development))["gunneryProfessorFollowUpBonusPoints"].Integer())
			+ "% of ordinary damage.";
	return true;
}

}
