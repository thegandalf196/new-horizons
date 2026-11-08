/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCreatureSpecialtyCapabilities.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../battle/NewHorizonsCreatureSpecialtyCapabilities.h"
#include "../../CCreatureHandler.h"
#include "../../bonuses/Bonus.h"
#include "../../bonuses/BonusParameters.h"
#include "../../bonuses/Limiters.h"
#include "../../nativeRules/NativeRulePilots.h"
#include <string_view>
namespace newHorizonsHeroes
{
namespace
{
using Bundle = std::vector<std::shared_ptr<Bonus>>;
CreatureID resolveCreature(const char * name)
{
	const CreatureID result(CreatureID::decode(name));
	if(!result.hasValue() || !result.toCreature())
		throw std::runtime_error("Native creature specialty capability family is not registered");
	return result;
}
std::shared_ptr<Bonus> familyBonus(const CGHeroInstance & hero, CreatureID family, BonusType type,
	int value, const char * operation)
{
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::HERO_SPECIAL,
		value, BonusSourceID(hero.getHeroTypeID()));
	bonus->stacking = "new-horizons:ordinary-capability:" + hero.getHeroType()->getJsonKey() + ':' + operation;
	bonus->limiter = std::make_shared<CCreatureTypeLimiter>(*family.toCreature(), false);
	return nativeRules::prepareFamilySpecialtyBonus(*bonus);
}
Bundle stonecaller(const CGHeroInstance & hero)
{
	using namespace newHorizonsCreatureSpecialtyCapabilities;
	const auto earth = resolveCreature("core:earthElemental");
	const auto magma = resolveCreature("core:magmaElemental");
	const auto subtype = BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(RangedProfileKind::Stonecaller)));
	auto family = std::make_shared<AnyOfLimiter>();
	family->add(std::make_shared<CCreatureTypeLimiter>(*earth.toCreature(), false));
	family->add(std::make_shared<CCreatureTypeLimiter>(*magma.toCreature(), false));
	Bundle result;
	for(const auto & row : {std::pair{BonusType::SHOOTER, 1}, {BonusType::SHOTS, STONECALLER_BASE_AMMUNITION},
		{BonusType::NO_MELEE_PENALTY, 1}})
	{
		auto bonus = familyBonus(hero, earth, row.first, row.second,
			row.first == BonusType::SHOOTER ? "stonecallerShooter" : row.first == BonusType::SHOTS ? "stonecallerAmmo" : "stonecallerMelee");
		bonus->limiter = family;
		bonus->subtype = subtype;
		if(row.first == BonusType::SHOOTER)
			bonus->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{
				STONECALLER_PROFILE_VERSION, STONECALLER_BASE_AMMUNITION, earth.getNum(), magma.getNum()});
		result.push_back(std::move(bonus));
	}
	return result;
}
Bundle archshaman(const CGHeroInstance & hero)
{
	const auto mage = resolveCreature("core:ogreMage");
	Bundle result;
	for(const auto spell : {SpellID(SpellID::BERSERK), SpellID(SpellID::MISFORTUNE)})
	{
		auto bonus = familyBonus(hero, mage, BonusType::SPELLCASTER, 2,
			spell == SpellID(SpellID::BERSERK) ? "archshamanBerserk" : "archshamanMisfortune");
		bonus->subtype = BonusSubtypeID(spell);
		result.push_back(std::move(bonus));
	}
	// CASTS and CREATURE_ENCHANT_POWER remain the creature's existing shared pool.
	return result;
}
Bundle goldStandard(const CGHeroInstance & hero)
{
	// Stable legacy keys are historically reversed: actual Iron Golem is index33.
	const auto iron = resolveCreature("core:stoneGolem");
	const auto gold = resolveCreature("core:goldGolem");
	auto bonus = familyBonus(hero, iron, BonusType::SPECIAL_UPGRADE, 0, "goldStandard");
	bonus->subtype = BonusSubtypeID(iron);
	bonus->parameters = std::make_shared<BonusParameters>(gold);
	return {bonus};
}
}
std::vector<std::shared_ptr<Bonus>> creatureSpecialtyCapabilityBonuses(const CGHeroInstance & hero)
{
	if(!hero.getHeroType())
		return {};
	const auto & policy = hero.getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	if(!policy.isStruct())
		return {};
	const std::string_view identity = hero.getHeroType()->getJsonKey();
	if(identity == "core:thunar" && policy.Struct().contains("stonecallerAmmunition"))
		return stonecaller(hero);
	if(identity == "core:vey" && policy.Struct().contains("archshamanSpellMastery"))
		return archshaman(hero);
	if(identity == "core:josephine" && policy.Struct().contains("goldStandardUpgrade"))
		return goldStandard(hero);
	return {};
}
}
