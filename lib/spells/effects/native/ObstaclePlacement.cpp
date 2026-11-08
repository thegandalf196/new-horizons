/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ObstaclePlacement.h"
#include "../../ISpellMechanics.h"
#include "../../../combatScripts/NativeJsonParameters.h"
#include <vcmi/spells/Spell.h>

namespace spells::effects::native
{
const JsonNode & ObstaclePlacement::options(BattleSide side) const
{
	return parameters[side == BattleSide::ATTACKER ? "attacker" : "defender"];
}

const JsonVector & ObstaclePlacement::paths(const JsonNode & node)
{
	static const JsonVector centerOnly{JsonNode()};
	return node.Vector().empty() ? centerOnly : node.Vector();
}

BattleHex ObstaclePlacement::shape(BattleHex hex, const JsonNode & path)
{
	for(const auto & item : path.Vector())
	{
		const auto & key = item.String();
		const auto direction = key == "TL" ? BattleHex::TOP_LEFT : key == "TR" ? BattleHex::TOP_RIGHT
			: key == "R" ? BattleHex::RIGHT : key == "BR" ? BattleHex::BOTTOM_RIGHT
			: key == "BL" ? BattleHex::BOTTOM_LEFT : key == "L" ? BattleHex::LEFT : BattleHex::NONE;
		if(direction != BattleHex::NONE) hex = hex.cloneInDirection(direction, false);
	}
	return hex;
}

bool ObstaclePlacement::landMine(const Mechanics & mechanics)
{
	return mechanics.usesNewHorizonsMagic() && mechanics.getSpell()->getJsonKey() == "core:landMine";
}
bool ObstaclePlacement::fireWall(const Mechanics & mechanics)
{
	return mechanics.usesNewHorizonsMagic() && mechanics.getSpell()->getJsonKey() == "core:fireWall";
}
bool ObstaclePlacement::quicksand(const Mechanics & mechanics)
{
	return mechanics.usesNewHorizonsMagicV3() && mechanics.getSpell()->getJsonKey() == "core:quicksand";
}
bool ObstaclePlacement::selectedQuicksand(const Mechanics & mechanics)
{
	return mechanics.usesNewHorizonsQuicksandSelectedPlacement() && mechanics.getSpell()->getJsonKey() == "core:quicksand";
}

bool ObstaclePlacement::selectedValid(const Mechanics & mechanics, const Target & target, int count)
{
	if(static_cast<int>(target.size()) != count) return false;
	std::bitset<GameConstants::BFIELD_SIZE> seen;
	for(const auto & destination : target)
	{
		const auto hex = destination.hexValue;
		if(destination.unitValue || !hex.isAvailable() || seen[hex.toInt()] || !structure::hexAvailable(*mechanics.battle(), hex, true)) return false;
		seen.set(hex.toInt());
	}
	return true;
}

bool ObstaclePlacement::applicableGeneral(Problem & problem, const Mechanics * mechanics) const
{
	if(parameters["hidden"].Bool() && parameters["hideNative"].Bool() && !landMine(*mechanics) && !selectedQuicksand(*mechanics))
	{
		const auto other = mechanics->getCasterSide() == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		if(mechanics->battle()->battleHasNativeStack(other)) return status::reject(mechanics, &problem);
	}
	return true;
}

bool ObstaclePlacement::applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const
{
	if(landMine(*mechanics)) return selectedValid(*mechanics, target, mechanics->getNewHorizonsLandMinePatchCount()) || status::reject(mechanics, &problem);
	if(selectedQuicksand(*mechanics))
	{
		const auto count = mechanics->getNewHorizonsQuicksandPatchCount();
		return (count > 0 && selectedValid(*mechanics, target, count)) || status::reject(mechanics, &problem);
	}
	if(fireWall(*mechanics)) return selectedValid(*mechanics, target, 3) || status::reject(mechanics, &problem);
	if(mechanics->isMassive()) return true;
	if(target.empty()) return status::reject(mechanics, &problem);
	for(const auto & destination : target)
		for(const auto & path : paths(options(mechanics->getCasterSide())["shape"]))
			if(!structure::hexAvailable(*mechanics->battle(), shape(destination.hexValue, path), true)) return status::reject(mechanics, &problem);
	return true;
}

Target ObstaclePlacement::transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const
{
	if(landMine(*mechanics) || selectedQuicksand(*mechanics) || fireWall(*mechanics)) return aim;
	if(mechanics->isMassive()) return {};
	Target result;
	for(const auto & destination : target)
		for(const auto & path : paths(options(mechanics->getCasterSide())["range"])) result.emplace_back(shape(destination.hexValue, path));
	return result;
}

void ObstaclePlacement::adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & target) const
{
	for(const auto & destination : transformTarget(mechanics, target, target))
		for(const auto & path : paths(options(mechanics->getCasterSide())["shape"]))
			if(const auto hex = shape(destination.hexValue, path); hex.isValid()) hexes.insert(hex);
}

SpellCreatedObstacle ObstaclePlacement::descriptor(const Mechanics & mechanics, BattleHex position, const BattleHexArray & footprint) const
{
	auto result = structure::obstacle(mechanics, position);
	const bool selected = selectedQuicksand(mechanics);
	const bool snapshot = landMine(mechanics) || fireWall(mechanics);
	// Lua's descriptor reader skips nil fields, retaining the default unscoped
	// null. Do not invent capture metadata for an absent penetration snapshot.
	const auto penetration = mechanics.getCapturedMdrPenetration();
	if(!penetration.isNull())
		result.capturedMdrPenetration = nativeRules::capturedParameters(penetration);
	result.minimalDamage = snapshot ? mechanics.scaleHeroLuckDamage(mechanics.getEffectValue()) : parameters["minimalDamage"].Integer();
	result.damageSnapshot = snapshot;
	result.turnsRemaining = fireWall(mechanics) ? mechanics.adjustMetamagicDuration(mechanics.adjustEffectDuration(3), nullptr)
		: nativeEvents::number(parameters, "turnsRemaining", -1);
	result.hidden = selected || parameters["hidden"].Bool();
	result.passable = parameters["passable"].Bool();
	result.nativeVisible = !(snapshot || selected || parameters["hideNative"].Bool());
	result.trap = parameters["trap"].Bool();
	result.removeOnTrigger = parameters["removeOnTrigger"].Bool();
	const auto trigger = parameters["triggerAbility"].String();
	result.trigger = trigger.empty() ? SpellID(SpellID::NONE) : SpellID(SpellID::decode(trigger));
	const auto & assets = options(mechanics.getCasterSide());
	result.appearSound = AudioPath::builtin(assets["appearSound"].String());
	result.appearAnimation = AnimationPath::builtin(assets["appearAnimation"].String());
	result.animation = AnimationPath::builtin(assets["animation"].String());
	result.removalAnimation = AnimationPath::builtin(assets["removalAnimation"].String());
	result.customSize = footprint;
	return result;
}
}
