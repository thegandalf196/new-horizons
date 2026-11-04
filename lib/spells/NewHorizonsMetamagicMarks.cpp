/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicMarks.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../entities/hero/CHero.h"
#include "../json/JsonNode.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace newHorizonsMetamagic
{
namespace
{
void validateMarks(int32_t marks, bool points = false)
{
	if(marks < 0 || marks > (points ? MAXIMUM_POINTS : MAXIMUM_MARKS))
		throw std::invalid_argument("Arcane Marks must be between zero and five");
}

bool active(const Profile & value)
{
	if(value.rank < 0 || value.rank > 3)
		throw std::invalid_argument("Arcane Marks require an ordinary Skill rank");
	if(value.adeptAdditionalMarks < 0 || value.adeptAdditionalMarks > 5)
		throw std::invalid_argument("Invalid Metamagic Adept additional marks");
	if(value.adeptAdditionalPoints < 0 || value.adeptAdditionalPoints > MAXIMUM_POINTS)
		throw std::invalid_argument("Invalid Metamagic Adept additional points");
	return value.rank > 0;
}
}

bool usesPoints(const JsonNode & rules)
{
	if(!rules.isStruct())
		return false;
	const auto & policy = rules["metamagicPolicy"];
	return policy.isStruct() && policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& policy["version"].Integer() == 3;
}

bool usesArcaneMarks(const JsonNode & capturedPerkRules)
{
	if(!capturedPerkRules.isStruct())
		return false;
	const auto & policy = capturedPerkRules["metamagicPolicy"];
	return policy.isStruct() && policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& policy["version"].Integer() == 2;
}

Profile profile(const CGHeroInstance * hero)
{
	Profile result;
	if(!hero || (!usesArcaneMarks(hero->getPerkState().rules) && !usesPoints(hero->getPerkState().rules)))
		return result;
	result.pointPolicy = usesPoints(hero->getPerkState().rules);
	if(result.pointPolicy && hero->getFactionID() != FactionID::TOWER)
		return {};
	result.rank = std::clamp(hero->getPerkSkillRank("new-horizons:metamagic"), 0, 3);
	if(result.rank == 0)
		return result;
	const auto has = [hero](const char * perk)
	{
		return hero->hasActivePerk("new-horizons:metamagic", perk);
	};
	if(result.pointPolicy)
	{
		result.arcaneCatalyst = has("new-horizons:metamagic.arcaneCatalyst");
		result.arcaneShield = has("new-horizons:metamagic.arcaneShield");
		result.manaConduit = has("new-horizons:metamagic.manaConduit");
		result.lingeringMagic = has("new-horizons:metamagic.lingeringMagic");
		result.arcaneFeedback = has("new-horizons:metamagic.arcaneFeedback");
		result.spellFracture = has("new-horizons:metamagic.spellFracture");
		result.arcaneWellspring = has("new-horizons:metamagic.arcaneWellspring");
		result.arcaneAttunement = has("new-horizons:metamagic.arcaneAttunement");
		result.arcaneEcho = has("new-horizons:metamagic.arcaneEcho");
		result.arcaneSingularity = has("new-horizons:metamagic.arcaneSingularity");
		const auto & policy = hero->getPerkState().rules["metamagicPolicy"];
		if(const auto * type = hero->getHeroType(); type && type->getJsonKey() == "core:halon"
			&& policy.Struct().contains("adeptAdditionalPoints"))
			result.adeptAdditionalPoints = policy["adeptAdditionalPoints"].Integer();
		return result;
	}
	result.arcaneConjuration = has("new-horizons:metamagic.arcaneConjuration");
	result.lingeringMagic = has("new-horizons:metamagic.lingeringMagic");
	result.manaAttunement = has("new-horizons:metamagic.manaAttunement");
	result.arcaneAmplification = has("new-horizons:metamagic.arcaneAmplification");
	result.residualMagic = has("new-horizons:metamagic.residualMagic");
	result.arcaneSaturation = has("new-horizons:metamagic.arcaneSaturation");
	result.arcaneVulnerability = has("new-horizons:metamagic.arcaneVulnerability");
	result.arcaneFortification = has("new-horizons:metamagic.arcaneFortification");
	result.perfectImprint = has("new-horizons:metamagic.perfectImprint");
	result.arcanePerfection = has("new-horizons:metamagic.arcanePerfection");
	const auto & policy = hero->getPerkState().rules["metamagicPolicy"];
	if(const auto * type = hero->getHeroType(); type && type->getJsonKey() == "core:halon"
		&& policy.Struct().contains("adeptAdditionalMarks"))
		result.adeptAdditionalMarks = policy["adeptAdditionalMarks"].Integer();
	return result;
}

int32_t pointsAtRoundStart(const Profile & value, int32_t previous, int32_t round)
{
	validateMarks(previous, true);
	if(round < 1)
		throw std::invalid_argument("Metamagic point round must be positive");
	if(!active(value) || !value.pointPolicy)
		return previous;
	return round == 1 ? (value.arcaneWellspring ? 3 : 1) : std::min(MAXIMUM_POINTS, previous + 1);
}

int32_t pointsAfterResolvedSpell(const Profile & value, int32_t previous, bool ownHero, bool manualHeroCast)
{
	validateMarks(previous, true);
	if(!active(value) || !value.pointPolicy || !manualHeroCast)
		return previous;
	const bool gain = ownHero ? value.arcaneCatalyst : value.arcaneFeedback;
	return std::min(MAXIMUM_POINTS, previous + (gain ? 1 : 0) + (ownHero ? value.adeptAdditionalPoints : 0));
}

int32_t manaConduitRecovery(const Profile & value, int32_t points)
{
	validateMarks(points, true);
	return active(value) && value.pointPolicy && value.manaConduit ? points / 3 : 0;
}

int32_t attunementInitiative(const Profile & value, int32_t points)
{
	validateMarks(points, true);
	return active(value) && value.pointPolicy && value.arcaneAttunement ? points / 4 : 0;
}

int64_t arcaneShieldCapacity(const Profile & value, int32_t points, int64_t maximumAggregateHealth)
{
	validateMarks(points, true);
	if(maximumAggregateHealth < 0)
		throw std::invalid_argument("Negative Arcane Shield health capacity");
	if(!active(value) || !value.pointPolicy || !value.arcaneShield)
		return 0;
	return maximumAggregateHealth / 100 * points + maximumAggregateHealth % 100 * points / 100;
}

int32_t singularityManaCost(int32_t ordinary, const Profile & value, bool reached)
{
	if(ordinary < 0)
		throw std::invalid_argument("Negative Arcane Singularity Mana cost");
	if(!active(value) || !value.pointPolicy || !value.arcaneSingularity || !reached)
		return ordinary;
	// Match native Mana rounding: ceiling after the complete discount.
	return static_cast<int32_t>((static_cast<int64_t>(ordinary) * 75 + 99) / 100);
}

int64_t echoDamage(int64_t originalDamage)
{
	if(originalDamage < 0)
		throw std::invalid_argument("Negative Arcane Echo damage");
	return originalDamage / 4;
}

int32_t castCounter(const CastProfile & cast, int32_t recipientMarks)
{
	return cast.perks.pointPolicy ? cast.points : recipientMarks;
}

int32_t effectivenessBonusPercent(const Profile & value, int32_t marks)
{
	validateMarks(marks, value.pointPolicy);
	if(!active(value))
		return value.wisdomEffectivenessBonus;
	if(value.pointPolicy)
		return value.rank * marks + value.wisdomEffectivenessBonus;
	const int32_t perMark = 5 * value.rank + (value.arcaneAmplification ? 2 : 0)
		+ (value.arcanePerfection ? 5 : 0);
	return perMark * marks + (value.arcaneSaturation && marks == MAXIMUM_MARKS ? 20 : 0) + value.wisdomEffectivenessBonus;
}

int32_t marksAfterResolvedSpell(const Profile & value, int32_t marks)
{
	validateMarks(marks, value.pointPolicy);
	if(!active(value) || value.pointPolicy)
		return marks;
	return std::min(MAXIMUM_MARKS, marks + (value.residualMagic && marks == 0 ? 2 : 1)
		+ value.adeptAdditionalMarks);
}

int32_t initialMarks(const Profile & value)
{
	return active(value) && !value.pointPolicy && value.perfectImprint ? 2 : 0;
}

int32_t summonedMarks(const Profile & value)
{
	return active(value) && !value.pointPolicy && value.arcaneConjuration ? 2 : 0;
}

int32_t additionalDuration(const Profile & value, int32_t marks)
{
	validateMarks(marks, value.pointPolicy);
	if(value.pointPolicy)
		return active(value) && value.lingeringMagic ? marks / 3 : 0;
	return active(value) && value.lingeringMagic && marks >= QUALIFYING_MARKS ? 2 : 0;
}

int32_t resistanceReductionPoints(const Profile & value, int32_t marks)
{
	validateMarks(marks, value.pointPolicy);
	return active(value) && !value.pointPolicy && value.arcaneVulnerability && marks >= QUALIFYING_MARKS ? 20 : 0;
}

int32_t enemySpellDamageReductionPercent(const Profile & value, int32_t marks)
{
	validateMarks(marks, value.pointPolicy);
	return active(value) && !value.pointPolicy && value.arcaneFortification && marks >= QUALIFYING_MARKS ? 20 : 0;
}

int64_t scaleMagnitude(int64_t ordinary, const Profile & value, int32_t marks)
{
	if(ordinary < 0)
		throw std::invalid_argument("Arcane Marks require a non-negative effect magnitude");
	const int64_t factor = 100 + effectivenessBonusPercent(value, marks);
	const int64_t whole = ordinary / 100;
	const int64_t tail = ordinary % 100 * factor / 100;
	const auto maximum = std::numeric_limits<int64_t>::max();
	if(whole > (maximum - tail) / factor)
		return maximum;
	return whole * factor + tail;
}

int64_t spellDamageOutput(int64_t ordinary, const Profile & value, int32_t marks,
	int32_t defensiveReductionPercent, int64_t offensiveDamageCap, int32_t additionalBonusPercent)
{
	if(ordinary < 0 || defensiveReductionPercent < 0 || defensiveReductionPercent > 100
		|| offensiveDamageCap < 0 || additionalBonusPercent < 0)
		throw std::invalid_argument("Invalid Metamagic spell-damage output context");
	int64_t result;
	if(additionalBonusPercent == 0)
		result = scaleMagnitude(ordinary, value, marks);
	else
	{
		const int64_t factor = 100LL + effectivenessBonusPercent(value, marks) + additionalBonusPercent;
		const auto tail = ordinary % 100 * factor / 100;
		const auto maximum = std::numeric_limits<int64_t>::max();
		result = ordinary / 100 > (maximum - tail) / factor ? maximum : ordinary / 100 * factor + tail;
	}
	if(offensiveDamageCap > 0)
		result = std::min(result, offensiveDamageCap);
	const int64_t remaining = 100 - defensiveReductionPercent;
	return result / 100 * remaining + result % 100 * remaining / 100;
}

int32_t scaleSignedBonus(int32_t ordinary, const Profile & value, int32_t marks)
{
	const int64_t product = static_cast<int64_t>(ordinary)
		* (100 + effectivenessBonusPercent(value, marks));
	// Floor the magnitude, then restore its sign: native signed division does exactly that.
	const int64_t floor = product / 100;
	return static_cast<int32_t>(std::clamp<int64_t>(floor,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

int32_t scaleDuration(int32_t ordinary, const Profile & value, int32_t marks)
{
	validateMarks(marks, value.pointPolicy);
	if(ordinary <= 0)
		return ordinary;
	const int64_t scaled = scaleMagnitude(ordinary, value, marks);
	const int64_t extra = additionalDuration(value, marks);
	return static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(), scaled + extra));
}

int32_t adjustedManaCost(int32_t ordinary, const Profile & value, uint32_t qualifying, uint32_t affected)
{
	if(ordinary < 0 || qualifying > affected)
		throw std::invalid_argument("Invalid Arcane Marks Mana-cost context");
	if(!active(value) || !value.manaAttunement || affected == 0 || qualifying == 0 || ordinary == 0)
		return ordinary;
	const int64_t denominator = 100LL * affected;
	const int64_t numerator = denominator - 15LL * qualifying;
	if(ordinary > std::numeric_limits<int64_t>::max() / numerator)
		throw std::overflow_error("Arcane Marks Mana-cost fraction overflow");
	const int64_t product = ordinary * numerator;
	return static_cast<int32_t>(product / denominator + (product % denominator != 0 ? 1 : 0));
}

int32_t maximumAffordableOvercharge(int32_t availableMana, int32_t ordinaryBaseCost,
	int32_t formulaMaximum, const Profile & value, uint32_t qualifying, uint32_t affected, bool singularityReached)
{
	if(availableMana < 0 || ordinaryBaseCost < 0 || formulaMaximum < 0)
		throw std::invalid_argument("Invalid Arcane Marks Overcharge context");
	const auto cost = [&](int32_t ordinary)
	{
		return value.pointPolicy ? singularityManaCost(ordinary, value, singularityReached)
			: adjustedManaCost(ordinary, value, qualifying, affected);
	};
	if(cost(ordinaryBaseCost) > availableMana)
		return -1;
	int32_t low = 0;
	int32_t high = std::min(formulaMaximum, std::numeric_limits<int32_t>::max() - ordinaryBaseCost);
	while(low < high)
	{
		const int32_t middle = static_cast<int32_t>(low + (static_cast<int64_t>(high) - low + 1) / 2);
		if(cost(ordinaryBaseCost + middle) <= availableMana)
			low = middle;
		else
			high = middle - 1;
	}
	return low;
}
}
