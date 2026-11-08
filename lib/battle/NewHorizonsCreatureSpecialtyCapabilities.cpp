/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCreatureSpecialtyCapabilities.h"
#include "Unit.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
namespace newHorizonsCreatureSpecialtyCapabilities
{
namespace
{
BonusSubtypeID profileSubtype()
{
	return BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(RangedProfileKind::Stonecaller)));
}
bool preparedPart(const Bonus & bonus, BonusType type, int32_t magnitude)
{
	return bonus.type == type && bonus.source == BonusSource::HERO_SPECIAL
		&& bonus.sid.as<HeroTypeID>().hasValue() && bonus.subtype == profileSubtype()
		&& bonus.val == magnitude && bonus.valType == BonusValueType::ADDITIVE_VALUE
		&& bonus.duration == BonusDuration::PERMANENT && bonus.effectRange == BonusLimitEffect::NO_LIMIT;
}
}
bool isStonecallerShooterProfile(const Bonus & bonus)
{
	if(bonus.type != BonusType::SHOOTER || bonus.source != BonusSource::HERO_SPECIAL || bonus.subtype != profileSubtype())
		return false;
	if(!preparedPart(bonus, BonusType::SHOOTER, 1) || !bonus.parameters || !bonus.parameters->isVector())
		throw std::runtime_error("Invalid prepared Stonecaller ranged profile");
	const auto & data = bonus.parameters->toVector();
	if(data.size() != 4 || data[0] != STONECALLER_PROFILE_VERSION || data[1] != STONECALLER_BASE_AMMUNITION
		|| data[2] < 0 || data[3] < 0 || data[2] == data[3])
		throw std::runtime_error("Unsupported prepared Stonecaller ranged profile");
	return true;
}
bool stonecallerRanged(const battle::Unit & unit)
{
	const auto shooters = unit.getBonusesBeforeCreatureAbilitySuppression(Selector::type()(BonusType::SHOOTER));
	for(const auto & shooter : *shooters)
	{
		if(!isStonecallerShooterProfile(*shooter))
			continue;
		const auto & data = shooter->parameters->toVector();
		if(unit.creatureId().getNum() != data[2] && unit.creatureId().getNum() != data[3])
			continue;
		const auto hasPart = [&](BonusType type, int32_t value)
		{
			const auto parts = unit.getBonusesBeforeCreatureAbilitySuppression(Selector::type()(type));
			return std::ranges::any_of(*parts, [&](const auto & part)
			{
				return preparedPart(*part, type, value) && part->sid == shooter->sid;
			});
		};
		if(hasPart(BonusType::SHOTS, STONECALLER_BASE_AMMUNITION) && hasPart(BonusType::NO_MELEE_PENALTY, 1))
			return true;
	}
	return false;
}
}
