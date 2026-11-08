/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsElementalAttunement.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../json/JsonNode.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace newHorizonsElementalAttunement
{
namespace
{
bool selected(const CGHeroInstance & hero, const char * suffix)
{
	return hero.hasActivePerk(SKILL_ID, std::string(SKILL_ID) + "." + suffix);
}
int rank(const CGHeroInstance * hero)
{
	if(!usesRules(hero) || hero->getFactionID() != FactionID::CONFLUX
		|| !newHorizonsHeroes::usesRules(hero->getPrimaryGrowthRules()))
		return 0;
	static const SecondarySkill skill(SecondarySkill::decode(SKILL_ID));
	const auto factionSkill = newHorizonsHeroes::factionSkill(hero->getPrimaryGrowthRules(), FactionID::CONFLUX);
	return skill.hasValue() && factionSkill && *factionSkill == skill
		? std::clamp<int>(hero->getSecSkillLevel(skill), 0, 3) : 0;
}
bool bird(CreatureID creature)
{
	const auto * type = creature.hasValue() ? creature.toCreature() : nullptr;
	return type && (type->getJsonKey() == "core:firebird" || type->getJsonKey() == "core:phoenix");
}
}

int stoneboundReductionBasisPoints(int finalSpellPower)
{
	return static_cast<int>(std::min<int64_t>(1500, 25LL * std::max(0, finalSpellPower)));
}

int64_t stoneboundPhysicalDamage(int64_t ordinary, int reductionBasisPoints)
{
	if(ordinary <= 0) return ordinary;
	const int multiplier = 10000 - std::clamp(reductionBasisPoints, 0, 1500);
	// Preserve floor rounding without overflowing on a large damage envelope.
	return ordinary / 10000 * multiplier + ordinary % 10000 * multiplier / 10000;
}

Profile captureProfile(const CGHeroInstance * hero)
{
	Profile result;
	result.rank = rank(hero);
	if(result.rank == 0)
		return result;
	result.magmaWeapons = selected(*hero, "elementalAttunement");
	result.stormBolts = selected(*hero, "swiftRebirth");
	result.elementalConduit = selected(*hero, "elementalConduit");
	result.waterVeil = selected(*hero, "primalBurst");
	result.greaterEssence = selected(*hero, "greaterEssence");
	result.stoneboundMagic = selected(*hero, "adaptiveElement");
	result.stoneboundBasisPoints = result.stoneboundMagic
		? stoneboundReductionBasisPoints(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)) : 0;
	result.primalVigilance = selected(*hero, "rebirthChain");
	result.elementalCataclysm = selected(*hero, "elementalCataclysm");
	result.validate();
	return result;
}

namespace
{
uint8_t creatureFamilyKey(std::string_view id)
{
	if(id == "core:airElemental" || id == "core:stormElemental") return 1;
	if(id == "core:fireElemental" || id == "core:energyElemental") return 2;
	if(id == "core:waterElemental" || id == "core:iceElemental") return 4;
	if(id == "core:earthElemental" || id == "core:magmaElemental") return 8;
	return 0;
}
}
uint8_t creatureFamilyMask(CreatureID creature)
{
	const auto * type = creature.hasValue() ? creature.toCreature() : nullptr;
	return type ? creatureFamilyKey(type->getJsonKey()) : 0;
}

bool eligibleCreature(CreatureID creature)
{
	return creatureFamilyMask(creature) != 0 || bird(creature);
}

uint8_t activatedMask(uint8_t priorMask, uint8_t elementMask)
{
	if((priorMask & ~ALL_ELEMENTS) != 0 || (elementMask & ~ALL_ELEMENTS) != 0)
		throw std::invalid_argument("Invalid Elemental Attunement family mask");
	return priorMask | elementMask;
}

uint8_t completedHeroCastMask(const CBattleInfoCallback & battle,
	BattleSide side, SpellID spell, uint8_t priorMask)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return priorMask;
	if(!battle.getBattle() || rank(battle.battleGetFightingHero(side)) == 0)
		return priorMask;
	return activatedMask(priorMask, spellElementMask(battle.getBattle()->getMagicRules(), spell));
}

UnitPassives passivesForCreatureKey(const Profile & profile, uint8_t mask, std::string_view creatureKey)
{
	UnitPassives result;
	if(profile.rank < 1 || profile.rank > 3) return result;
	result.physicalReductionBasisPoints = std::clamp(profile.stoneboundBasisPoints, 0, 1500);
	const bool isBird = creatureKey == "core:firebird" || creatureKey == "core:phoenix";
	const auto family = creatureFamilyKey(creatureKey);
	if(!family && !isBird) return result;
	result.maximumHealthPercent = profile.greaterEssence ? 25 : 0;
	result.attuned = (mask & (isBird ? ALL_ELEMENTS : family)) != 0;
	if(!result.attuned) return result;
	const int ordinary = 10 + 10 * profile.rank;
	result.meleeDamagePercent = ordinary + (profile.magmaWeapons ? 20 : 0);
	result.rangedDamagePercent = ordinary + (profile.stormBolts ? 20 : 0);
	result.spellReductionBasisPoints = profile.waterVeil ? 2000 : 0;
	result.additionalRetaliations = profile.primalVigilance ? 1 : 0;
	return result;
}

UnitPassives passivesForCreature(const Profile & profile, uint8_t mask, CreatureID creature)
{
	if(profile.rank == 0) return {};
	const auto * type = creature.hasValue() ? creature.toCreature() : nullptr;
	return passivesForCreatureKey(profile, mask, type ? type->getJsonKey() : std::string_view{});
}

int spellDamageBonusPercent(const Profile & profile, const JsonNode & rules, SpellID spell)
{
	return (profile.elementalConduit ? 15 : 0)
		+ (profile.elementalCataclysm && elementalAreaDamage(rules, spell) ? 25 : 0);
}

bool perfectConvergence(const CGHeroInstance * hero)
{
	return usesRules(hero) && hero->getFactionID() == FactionID::CONFLUX
		&& hero->getPerkSkillRank(SKILL_ID) >= 3 && selected(*hero, "perfectConvergence");
}

void applyLeadershipPolicy(newHorizonsHeroes::LeadershipSlotCapacity & capacity,
	const CGHeroInstance & hero, CreatureID creature)
{
	if(!usesRules(&hero) || !eligibleCreature(creature)
		|| hero.getFactionID() != FactionID::CONFLUX || !selected(hero, "phoenixSpark"))
		return;
	capacity.requirement = static_cast<int>(std::max<int64_t>(1,
		(static_cast<int64_t>(capacity.requirement) * 80 + 99) / 100));
	capacity.maximum = capacity.leadership / capacity.requirement;
}
}
