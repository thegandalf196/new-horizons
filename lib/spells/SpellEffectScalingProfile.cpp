/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpellEffectScalingProfile.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace newHorizonsMetamagic
{
namespace
{
void validate(const Profile & profile)
{
	if(profile.wisdomEffectivenessBonus < 0 || profile.wisdomEffectivenessBonus > 1000)
		throw std::invalid_argument("Invalid captured Wisdom effectiveness contribution");
}
}

int64_t scaleMagnitude(int64_t ordinary, const Profile & value)
{
	validate(value);
	if(ordinary < 0)
		throw std::invalid_argument("Spell scaling requires a non-negative effect magnitude");
	const int64_t factor = 100LL + value.wisdomEffectivenessBonus;
	const int64_t whole = ordinary / 100;
	const int64_t tail = ordinary % 100 * factor / 100;
	const auto maximum = std::numeric_limits<int64_t>::max();
	if(whole > (maximum - tail) / factor)
		return maximum;
	return whole * factor + tail;
}

int64_t spellDamageOutput(int64_t ordinary, const Profile & value,
	int64_t offensiveDamageCap, int32_t additionalBonusPercent)
{
	validate(value);
	if(ordinary < 0
		|| offensiveDamageCap < 0 || additionalBonusPercent < 0)
		throw std::invalid_argument("Invalid spell-damage output context");
	int64_t result;
	if(additionalBonusPercent == 0)
		result = scaleMagnitude(ordinary, value);
	else
	{
		const int64_t factor = 100LL + value.wisdomEffectivenessBonus + additionalBonusPercent;
		const auto tail = ordinary % 100 * factor / 100;
		const auto maximum = std::numeric_limits<int64_t>::max();
		result = ordinary / 100 > (maximum - tail) / factor ? maximum : ordinary / 100 * factor + tail;
	}
	if(offensiveDamageCap > 0)
		result = std::min(result, offensiveDamageCap);
	return result;
}

int32_t scaleSignedBonus(int32_t ordinary, const Profile & value)
{
	validate(value);
	const int64_t product = static_cast<int64_t>(ordinary)
		* (100LL + value.wisdomEffectivenessBonus);
	// Floor the magnitude, then restore its sign: native signed division does exactly that.
	const int64_t floor = product / 100;
	return static_cast<int32_t>(std::clamp<int64_t>(floor,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

int32_t scaleDuration(int32_t ordinary, const Profile & value)
{
	if(ordinary <= 0)
		return ordinary;
	const int64_t scaled = scaleMagnitude(ordinary, value);
	return static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(), scaled));
}

}
