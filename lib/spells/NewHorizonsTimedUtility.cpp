/*
 * NewHorizonsTimedUtility, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsTimedUtility.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusCustomTypes.h"
namespace newHorizonsMagic
{
std::optional<int32_t> configuredHolyArmorBase(const CSpell & spell, int32_t effectLevel)
{
	std::optional<int32_t> result;
	const auto & effects = spell.getLevelInfo(std::clamp(effectLevel, 0, 3)).battleEffects;
	for(const auto & [effectName, effect] : effects.Struct())
	{
		if(effect["type"].String() != "timed" && effect["type"].String() != "core:timed")
			continue;
		for(const auto & [name, definition] : effect["bonus"].Struct())
		{
			// Runtime decode reads resolved identities. The content loader's parseBonus
			// registers deferred identifier callbacks and is not a read-only UI API.
			const auto type = static_cast<BonusType>(BonusTypeID::decode(definition["type"].String()));
			if(type != BonusType::SPELL_DAMAGE_REDUCTION)
				continue;
			const auto subtype = decodeBonusSubtype(type, definition["subtype"].String());
			if(result || subtype != BonusSubtypeID(SpellSchool::ANY))
				return std::nullopt;
			// Same numeric conversion used by the existing typed Bonus loader.
			result = static_cast<int32_t>(definition["val"].Float());
		}
	}
	return result;
}

namespace
{
int64_t aegisPowerTerm(int64_t numerator, int32_t divisor, int32_t coefficient,
	int32_t warcasting, int32_t empower, bool aegis, int32_t crownAndAltar)
{
	if(crownAndAltar < 0 || crownAndAltar > 1000)
		throw std::invalid_argument("Invalid Holy Armor recipient multiplier");
	if(aegis)
	{
		numerator *= 120;
		divisor *= 100;
	}
	if(crownAndAltar != 0)
	{
		numerator *= 100 + crownAndAltar;
		divisor *= 100;
	}
	// Current Holy Armor deliberately has no generic non-damage specialty rider;
	// the original recipient scaler admits that rider only for Crusade.
	return spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		numerator, divisor, coefficient, warcasting, empower);
}
}
int64_t holyArmorReduction(int32_t configuredBase, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, bool aegis, int32_t crownAndAltar)
{
	return std::min<int64_t>(60, static_cast<int64_t>(configuredBase)
		+ aegisPowerTerm(power, 5, coefficient, warcasting, empower, aegis, crownAndAltar));
}
int64_t heavenlyGaleReductionBasisPoints(int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, bool aegis, int32_t crownAndAltar)
{
	return std::min<int64_t>(8000, 5000
		+ aegisPowerTerm(15LL * power, 1, coefficient, warcasting, empower, aegis, crownAndAltar));
}

int32_t forgetfulnessBaseDuration(int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower)
{
	const auto component = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		std::max(0, power), 80, coefficient, warcasting, empower);
	return static_cast<int32_t>(std::min<int64_t>(3, 1 + component));
}

int32_t landMineCountForHero(int32_t power, int32_t coefficient, const CGHeroInstance * hero)
{
	return landMineHexCount(power, coefficient) + static_cast<int>(hero
		&& hero->hasActivePerk(std::string(HAVOC_MAGIC_SKILL), "new-horizons:havocMagic.mineLayer"));
}

int32_t hexOfPainDamageSharePercent()
{
	return 10;
}

int32_t durationWithCastExtensions(int32_t base, bool extend, bool echoedDuration)
{
	const int extra = static_cast<int>(extend && base > 0) + static_cast<int>(echoedDuration);
	if(base < std::numeric_limits<int32_t>::max())
		base += std::min(extra, std::numeric_limits<int32_t>::max() - std::max(base, 0));
	return base;
}
}
