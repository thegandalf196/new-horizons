/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../nativeRules/PreparedPilotRules.h"
#include <unordered_map>
#include "NewHorizonsOrdinaryCreatureSpecialties.h"
#include "NewHorizonsPassiveSpecialtyRules.h"
#include "../../battle/NewHorizonsPassiveSpecialties.h"
#include "OrdinaryCreatureSpecialtyRegistry.h"
#include "../../nativeRules/NativeRulePilots.h"
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
const detail::OrdinaryCreatureSpecialty * ordinaryIdentity(std::string_view hero, const JsonNode & development, bool prepared = false)
{
	if(!usesRules(development) || !ordinaryPolicy(development))
		return nullptr;
	// Hero captures are admitted by resolveHeroRules and the owning read codec.
	// Keep this query bounded to identity and its captured coefficient.
	// Index immutable authored identities only; never cache a hero's captured rules.
	static const auto identities = []
	{
		std::unordered_map<std::string_view, const detail::OrdinaryCreatureSpecialty *> result;
		for(const auto & row : detail::ordinaryCreatureSpecialtyRegistry())
			result.emplace(row.hero, &row);
		return result;
	}();
	const auto found = identities.find(hero);
	return found == identities.end()
		|| (!prepared && !ordinaryPolicy(development)->Struct().contains(found->second->coefficient)) ? nullptr : found->second;
}
int valueForIdentity(const CGHeroInstance * hero, std::string_view expectedIdentity)
{
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != expectedIdentity)
		return 0;
	if(const auto * prepared = hero->getPreparedCreatureMechanics()) return prepared->bonus().val;
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
	detail::fields(*policy, {
		"relentlessBarrageDamagePercent", "pointBlankShotDamageBonusPercent", "cripplingBoltSpeedReduction",
		"cripplingBoltDuration", "incendiaryShotPolicyVersion", "ogreBloodlustPolicyVersion", "runicReflectionPolicyVersion",
		"stonecallerAmmunition", "archshamanSpellMastery", "goldStandardUpgrade",
		"bloodScentMaximumDamagePercent", "treantmootProtection", "chillingPresenceMoraleReduction",
		"chillingPresenceLuckReduction", "boneFormationFirstBandSize", "boneFormationFirstBandLimit",
		"boneFormationSecondBandSize", "boneFormationSecondBandLimit", "boneFormationRemainingBandSize",
		"perfectMountMagicalDamageImmunity", "furyOfTheHordeAttack", "furyOfTheHordeDefense",
		"furyOfTheHordeDamage", "orcRageMaximumDamagePercent", "wyvernVenomPolicyVersion",
		"dispellingShotMageChancePercent", "dispellingShotArchMageChancePercent", "cyclopeanStunChancePercent",
		"version", "marshSharpshooter", "retaliationDamagePercent",
		"maximumHealthPercent", "resistanceAuraPoints",
		"massedSpearsDamagePercent", "packPursuitSpeed",
		"chillingPresenceReductionPercent",
		"stoneSentinelReductionPercent", "undertowDefensePerAttack",
		"goblinFrenzyDamagePercent",
		"prismaticScreenReductionPercent", "evasiveFlightReductionPercentPerHex", "ridersExploitDamagePercent", "risingTideCreaturesPerRound", "heavyBatterySiegeTermBonusPercent", "shieldAndSwordPhysicalReductionPercent", "undyingHealingHP",
		"scorchingImpactDamageReductionPercent", "cruelOverseerDefenseReduction", "lanceFormationDefensePerHex",
		"lanceFormationMaximumDefense", "stormWingsDefenseIgnorePercent", "stormWingsMinimumFlightDistance",
		"consecratedTargetDefenseIgnorePercent", "featheredFeintPhysicalReductionIgnorePercent", "demonicSolidarityReductionPercent",
		"ironHideReductionPercent", "graveDustHealingReductionBasisPoints", "cinderReprisalDamagePercent",
		"emberbrandDamagePercent", "toxicStingChancePercent", "gnollHarriersDamagePercent",
		"prismaticVanguardPhysicalDamagePercent", "siegeSapperCreatureDefenseIgnorePercent"});
	detail::require(detail::integer((*policy)["version"], 1, 1), "ordinary creature specialty version");
	validatePassiveSpecialtyPolicy(*policy);
	if(policy->Struct().contains("packPursuitSpeed"))
		detail::require(detail::integer((*policy)["packPursuitSpeed"], 5, 5), "Pack Pursuit Speed");
	for(const auto & [key, value] : {std::pair{"undertowDefensePerAttack", 1},
		std::pair{"goblinFrenzyDamagePercent", 50}, std::pair{"evasiveFlightReductionPercentPerHex", 3},
		std::pair{"risingTideCreaturesPerRound", 1}, std::pair{"undyingHealingHP", 100}})
		if(policy->Struct().contains(key))
			detail::require(detail::integer((*policy)[key], value, value), "native event specialty coefficient");
	for(const char * key : {"marshSharpshooter"})
		detail::require(detail::integer((*policy)[key], 1, 1), "ordinary creature penalty waiver");
	for(const char * key : {"retaliationDamagePercent", "maximumHealthPercent"})
		detail::require(detail::integer((*policy)[key], 0, 100), "ordinary creature specialty coefficient");
	if(!nativeRules::hasPreparedCreatureRow(development, "core:clancy"))
		detail::require(detail::integer((*policy)["resistanceAuraPoints"], 0, 100), "ordinary resistance aura coefficient");
	for(const char * key : {"massedSpearsDamagePercent", "packPursuitSpeed",
		"chillingPresenceReductionPercent",
		"stoneSentinelReductionPercent", "undertowDefensePerAttack",
		"goblinFrenzyDamagePercent",
		"prismaticScreenReductionPercent", "evasiveFlightReductionPercentPerHex", "ridersExploitDamagePercent", "risingTideCreaturesPerRound"})
		if(policy->Struct().contains(key))
			detail::require(detail::integer((*policy)[key], 0, 100), "conditional ordinary creature damage coefficient");
	for(const char * key : {"shieldAndSwordPhysicalReductionPercent", "undyingHealingHP"})
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
	for(const char * key : {"massedSpearsDamagePercent", "packPursuitSpeed",
		"chillingPresenceReductionPercent",
		"stoneSentinelReductionPercent", "undertowDefensePerAttack",
		"goblinFrenzyDamagePercent"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Extended ordinary creature policies require adjacency-format support");
}

void validateOrdinaryCreaturePolicyExtensionsSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"prismaticScreenReductionPercent", "evasiveFlightReductionPercentPerHex",
		"ridersExploitDamagePercent", "risingTideCreaturesPerRound"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Captured ordinary creature policy extensions require format1146");
}

void validateOrdinaryRangedSpecialtiesSerialization(const JsonNode &, bool)
{
	// No current recipe remains in this historical codec family.
}

void validateOrdinaryMaintenanceSpecialtiesSerialization(const JsonNode &, bool)
{
	// No current recipe remains in this historical codec family.
}

void validateOrdinaryMachineSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"heavyBatterySiegeTermBonusPercent"})
		if(policy->Struct().contains(key))
			throw std::runtime_error("Captured ordinary machine specialties require format1150");
}

void validateOrdinaryCombatSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	const auto * policy = ordinaryPolicy(development);
	if(!policy || supported)
		return;
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	for(const char * key : {"shieldAndSwordPhysicalReductionPercent", "undyingHealingHP"})
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
			throw std::runtime_error("Captured extended ordinary specialties require format1153");
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
		&& (hero->getPreparedCreatureMechanics() || ordinaryIdentity(hero->getHeroType()->getJsonKey(), hero->getPrimaryGrowthRules()));
}

int ordinaryCreatureSpecialtyValue(const CBattleInfoCallback & battle, const battle::Unit & actor,
	std::string_view expectedIdentity)
{
	return valueForIdentity(ordinaryCreatureVisibleOwnerHero(battle, actor), expectedIdentity);
}
std::shared_ptr<Bonus> ordinaryCreatureSpecialtyBonus(const CGHeroInstance & hero)
{
	if(const auto * prepared = hero.getPreparedCreatureMechanics()) return prepared->attach();
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
	if(std::string_view(row->hero) == "core:ryland" || std::string_view(row->hero) == "core:galthran"
		|| std::string_view(row->hero) == "core:charna" || std::string_view(row->hero) == "core:vey"
		|| std::string_view(row->hero) == "core:pasis")
	{
		const auto heroID = std::string_view(row->hero);
		const auto aura = heroID == "core:galthran" ? nativeRules::AuraPolicy::NativeUndeadDefense
			: heroID == "core:charna" ? nativeRules::AuraPolicy::EnemyMeleeReduction
			: heroID == "core:pasis" ? nativeRules::AuraPolicy::DirectMagicProtection
			: heroID == "core:vey" ? nativeRules::AuraPolicy::FriendlyMeleeDamage
			: nativeRules::AuraPolicy::FriendlyDefense;
		nativeRules::configureAdjacentSpecialtyBonus(*bonus, creature, aura);
		return nativeRules::prepareAdjacentSpecialtyBonus(*bonus);
	}
	return std::string_view(row->hero) == "core:fiona"
		? nativeRules::prepareFamilySpecialtyBonus(*bonus) : bonus;
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

int ordinaryCreatureAttackPercent(const CGHeroInstance * hero, const BattleAttackInfo & attack)
{
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != "core:shakti"
		|| !attack.attacker || !attack.defender || !attack.physicalDamage || attack.shooting
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0)
		return 0;
	const auto * identity = ordinaryIdentity("core:shakti", hero->getPrimaryGrowthRules());
	const auto family = CreatureID(CreatureID::decode("core:troglodyte"));
	const auto creature = attack.attacker->creatureId();
	return identity && (creature == family || family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
		&& attack.attacker->getCount() > attack.defender->getCount()
		? static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[identity->coefficient].Integer()) : 0;
}

int ordinaryCreatureDamageMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(attack.attacker && attack.attacker->hasBonusOfType(BonusType::CASUALTY_DAMAGE_PERCENTAGE))
		return 100 + newHorizonsPassiveSpecialties::casualtyDamagePercent(attack);
	if(const int harriers = newHorizonsGnollHarriers::damagePercent(battle, attack); harriers > 0)
		return 100 + harriers;
	if(!attack.attacker || !attack.defender || !attack.physicalDamage || attack.shooting
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender))
		return 100;
	const auto * hero = ordinaryCreatureVisibleOwnerHero(battle, *attack.attacker);
	if(!hero || !hero->getHeroType())
		return 100;
	const auto key = hero->getHeroType()->getJsonKey();
	const auto * identity = ordinaryIdentity(key, hero->getPrimaryGrowthRules());
	if(identity && key == "core:tyraxor")
	{
		const auto family = CreatureID(CreatureID::decode(identity->creature));
		const auto creature = attack.attacker->creatureId();
		if(creature != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
			return 100;
		const auto * victimState = dynamic_cast<const battle::CUnitState *>(attack.defender);
		const bool qualifies = attack.cleaveDamagePercent == 0 && victimState && !victimState->counterAttacks.canUse();
		return 100 + (qualifies ? static_cast<int>((*ordinaryPolicy(hero->getPrimaryGrowthRules()))[identity->coefficient].Integer()) : 0);
	}
	return 100 + ordinaryCreatureRetaliationPercent(hero, attack)
		+ ordinaryCreatureAttackPercent(hero, attack);
}

int ordinaryCreatureProtectionPercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{

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

std::shared_ptr<Bonus> ordinaryCreatureStrikeBonus(const CGHeroInstance *, const BattleAttackInfo &)
{
	// Aeris is passive magical-damage immunity; Theodorus uses the native proc.
	return nullptr;
}

std::shared_ptr<Bonus> ordinaryCreatureStrikeBonus(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender)
		return nullptr;
	return ordinaryCreatureStrikeBonus(ordinaryCreatureVisibleOwnerHero(battle, *attack.attacker), attack);
}

bool ordinaryCreatureDamageIsStateful(const CBattleInfoCallback & battle)
{
	for(const auto * unit : battle.battleGetAllUnits(false))
		if(unit && newHorizonsPassiveSpecialties::hasDynamicDamagePassive(*unit))
			return true;
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

bool resolveOrdinaryCreatureSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	SpecialtyPresentation & result)
{
	// This public authoring/presentation boundary also accepts uncaptured JSON.
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	const auto prepared = nativeRules::preparePilotCreature(development, hero.getId());
	const auto * row = ordinaryIdentity(hero.getJsonKey(), development, static_cast<bool>(prepared));
	if(!row)
		return false;
	result.name = row->name;
	result.artwork = row->artwork;
	result.creature = CreatureID(CreatureID::decode(row->creature));
	result.description = row->description;
	result.summary = row->summary;
	const auto placeholder = result.description.find("%VALUE%");
	if(placeholder != std::string::npos)
	{
		const auto captured = prepared ? prepared->bonus().val : (*ordinaryPolicy(development))[row->coefficient].Integer();
		const auto displayed = captured;
		result.description.replace(placeholder, 7, std::to_string(displayed));
		if(const auto summaryPlaceholder = result.summary.find("%VALUE%"); summaryPlaceholder != std::string::npos)
			result.summary.replace(summaryPlaceholder, 7, std::to_string(displayed));
	}
	return true;
}

}
