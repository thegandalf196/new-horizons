/*
 * NewHorizonsImplosion.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsImplosion.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsSorcery.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/BattleDisplacementCause.h"
#include "../battle/BattleHexArray.h"
#include "../battle/Unit.h"

#include <array>
#include <tuple>
#include <boost/multiprecision/cpp_int.hpp>

namespace newHorizonsImplosion
{
namespace
{
constexpr std::array<BattleHex::EDir, 6> CLOCKWISE = {
	BattleHex::TOP_RIGHT, BattleHex::RIGHT, BattleHex::BOTTOM_RIGHT,
	BattleHex::BOTTOM_LEFT, BattleHex::LEFT, BattleHex::TOP_LEFT};

const JsonNode & row(const JsonNode & rules)
{
	return rules["spells"]["core:implosion"];
}

int32_t integer(const JsonNode & node, int32_t minimum, int32_t maximum)
{
	if(node.getType() != JsonNode::JsonType::DATA_INTEGER
		|| node.Integer() < minimum || node.Integer() > maximum)
		throw std::runtime_error("Unsupported New Horizons Implosion integer");
	return static_cast<int32_t>(node.Integer());
}

// Axial coordinates used by BattleHex::getDistance. Walk each ring clockwise,
// beginning at its NE corner; virtual coordinates avoid off-board wraparound.
int clockwiseIndex(BattleHex origin, BattleHex hex)
{
	const int distance = BattleHex::getDistance(origin, hex);
	if(distance == 0)
		return 0;
	const int q = hex.getX() + hex.getY() / 2 - origin.getX() - origin.getY() / 2;
	const int r = hex.getY() - origin.getY();
	constexpr std::array<std::pair<int, int>, 6> ringSteps = {{
		{1, 1}, {0, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, 0}}};
	int x = 0;
	int y = -distance;
	int index = 0;
	for(const auto & [dx, dy] : ringSteps)
		for(int step = 0; step < distance; ++step, ++index)
		{
			if(x == q && y == r)
				return index;
			x += dx;
			y += dy;
		}
	throw std::logic_error("Implosion hex ring mismatch");
}

bool isSpellLocked(const battle::Unit & unit)
{
	// Exact compatibility predicate used by BattleSpellMechanics, whose helper
	// is TU-local. Do not infer a magic seal from generic resistance or duration.
	static const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	const auto markers = unit.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellLock)));
	return markers && std::any_of(markers->begin(), markers->end(), [](const auto & marker)
	{
		return marker && marker->type == BonusType::MAGIC_RESISTANCE
			&& Bonus::NTurns(marker.get()) && marker->turnsRemain > 0;
	});
}

std::pair<int, int> footprintKey(const battle::Unit & unit, BattleHex origin, BattleHex head)
{
	std::pair<int, int> result{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
	for(const auto hex : unit.getHexes(head))
		if(hex.isAvailable())
			result = std::min(result, std::pair<int, int>{
				BattleHex::getDistance(origin, hex), clockwiseIndex(origin, hex)});
	return result;
}


}

bool hasRules(const JsonNode & rules)
{
	return row(rules).isStruct() && row(rules).Struct().contains("implosion");
}

void validate(const JsonNode & rules)
{
	if(!hasRules(rules))
		return;
	const auto & value = row(rules)["implosion"];
	if(!newHorizonsMagic::rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !value.isStruct() || value.Struct().size() != 5)
		throw std::runtime_error("Unsupported New Horizons Implosion rules");
	const int base = integer(value["baseBasisPoints"], 0, 10000);
	integer(value["powerBasisPointsPerSpellPower"], 1, 10000);
	integer(value["maximumBasisPoints"], base, 10000);
	integer(value["pullRadius"], 1, GameConstants::BFIELD_HEIGHT);
	integer(value["pullDistance"], 1, 1);
}

std::optional<Rules> rulesFor(const JsonNode & rules, SpellID spell)
{
	if(spell != SpellID::IMPLOSION || !hasRules(rules))
		return std::nullopt;
	validate(rules);
	if(!newHorizonsMagic::spellAllowedBySavedRoster(rules, spell))
		return std::nullopt;
	const auto & value = row(rules)["implosion"];
	return Rules{static_cast<int32_t>(value["baseBasisPoints"].Integer()),
		static_cast<int32_t>(value["powerBasisPointsPerSpellPower"].Integer()),
		static_cast<int32_t>(value["maximumBasisPoints"].Integer()),
		static_cast<int32_t>(value["pullRadius"].Integer()),
		static_cast<int32_t>(value["pullDistance"].Integer())};
}

int64_t damage(const Rules & rules, int64_t health, int32_t power,
	int32_t coefficient, int32_t warcasting, int32_t empower)
{
	if(health < 0 || power < 0 || coefficient < 10000 || coefficient > 1000000
		|| warcasting < 0 || warcasting > 200 || empower < 0 || empower > 100
		|| rules.baseBasisPoints < 0 || rules.maximumBasisPoints < rules.baseBasisPoints
		|| rules.maximumBasisPoints > 10000 || rules.powerBasisPointsPerSpellPower < 1
		|| rules.powerBasisPointsPerSpellPower > 10000)
		throw std::invalid_argument("Invalid Implosion damage inputs");
	// Canonical SP is the raw captured attribute, not legacy primary-unit conversion.
	// Fixed-width integer arithmetic retains fractional rank/modifiers through
	// the sole final floor. Even maximal int64 health and int32 inputs fit 256 bits.
	using Wide = boost::multiprecision::uint256_t;
	const Wide denominator = 100000000;
	const Wide base = Wide(rules.baseBasisPoints) * denominator;
	const Wide term = Wide(power) * rules.powerBasisPointsPerSpellPower
		* coefficient * (100 + warcasting) * (100 + empower);
	const Wide cap = Wide(rules.maximumBasisPoints) * denominator;
	const Wide numerator = std::min(Wide(base + term), cap);
	return (Wide(health) * numerator / (denominator * 10000)).convert_to<int64_t>();
}

std::vector<uint32_t> pullOrder(const CBattleInfoCallback & battle,
	uint32_t primaryID, BattleHex origin, int32_t radius)
{
	std::vector<std::tuple<int, int, uint32_t>> ordered;
	for(const auto * unit : battle.battleGetUnitsIf([](const battle::Unit * unit)
		{ return unit && unit->alive() && !unit->isGhost(); }))
	{
		if(unit->unitId() == primaryID || !unit->getPosition().isAvailable() || isSpellLocked(*unit))
			continue;
		const auto [distance, angle] = footprintKey(*unit, origin, unit->getPosition());
		if(distance <= radius)
			ordered.emplace_back(distance, angle, unit->unitId());
	}
	std::sort(ordered.begin(), ordered.end());
	std::vector<uint32_t> result;
	for(const auto & [distance, angle, id] : ordered)
		result.push_back(id);
	return result;
}

std::optional<BattleHex> pullDestination(const CBattleInfoCallback & battle,
	const battle::Unit & unit, BattleHex origin)
{
	if(isSpellLocked(unit))
		return std::nullopt;
	const int oldDistance = footprintKey(unit, origin, unit.getPosition()).first;
	int bestDistance = oldDistance;
	std::optional<BattleHex> result;
	for(const auto direction : CLOCKWISE)
	{
		const auto candidate = unit.getPosition().cloneInDirection(direction, false);
		if(!candidate.isAvailable())
			continue;
		const int distance = footprintKey(unit, origin, candidate).first;
		if(distance < bestDistance && battle.battleCanForciblyDisplace(&unit, candidate,
			BattleDisplacementCause::MAGICAL))
		{
			bestDistance = distance;
			result = candidate;
		}
	}
	return result;
}
}
