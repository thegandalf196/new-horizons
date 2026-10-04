/*
 * NewHorizonsPuppetMaster.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "NewHorizonsPuppetMaster.h"

#include "CBattleInfoEssentials.h"
#include "Unit.h"
#include "../bonuses/BonusSelector.h"
#include "../bonuses/BonusList.h"

namespace
{
SpellID puppetMasterSpellId()
{
	static const SpellID spell(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
	return spell;
}

SpellID luciditySpellId()
{
	static const SpellID spell(SpellID::decode(std::string(newHorizonsPuppetMaster::LUCIDITY_SPELL_ID)));
	return spell;
}
}

namespace newHorizonsPuppetMaster
{
bool hasControlMarker(const battle::Unit * unit)
{
	return unit && unit->hasBonusOfType(BonusType::PUPPET_MASTER_CONTROL);
}

bool hasValidControlMarker(const CBattleInfoEssentials & battle, const battle::Unit * unit)
{
	if(!unit)
		return false;
	const auto markers = unit->getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	return markers && std::ranges::any_of(*markers, [&battle, unit](const auto & marker)
	{
		return marker && isValidControlMarker(battle, unit, marker.get());
	});
}

bool hasLucidity(const battle::Unit * unit)
{
	return unit && unit->hasBonusOfType(BonusType::LUCIDITY);
}

bool isMentalControlSpell(std::string_view spellJsonKey)
{
	return spellJsonKey == SPELL_ID
		|| spellJsonKey == "core:berserk"
		|| spellJsonKey == "new-horizons:confusion";
}

bool isValidControlMarker(const CBattleInfoEssentials & battle, const battle::Unit * unit,
	const Bonus * marker)
{
	if(!unit || !marker || marker->type != BonusType::PUPPET_MASTER_CONTROL || marker->val != 1
		|| marker->source != BonusSource::SPELL_EFFECT
		|| marker->duration != BonusDuration::ONE_BATTLE
		|| marker->sid != BonusSourceID(puppetMasterSpellId())
		|| !marker->spellCasterOwner.isValidPlayer())
		return false;

	const auto controllerSide = battle.playerToSide(marker->spellCasterOwner);
	return (controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
		&& controllerSide != unit->unitSide();
}

Bonus controlMarker(SpellID spell, PlayerColor caster)
{
	Bonus marker(BonusDuration::ONE_BATTLE, BonusType::PUPPET_MASTER_CONTROL,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(spell));
	marker.spellCasterOwner = caster;
	marker.description.appendRawString("Puppet Master");
	return marker;
}

Bonus lucidityMarker()
{
	Bonus marker(BonusDuration::N_TURNS, BonusType::LUCIDITY,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(luciditySpellId()));
	marker.turnsRemain = 2;
	marker.description.appendRawString("Lucidity");
	return marker;
}

ActionControllerCaster::ActionControllerCaster(const spells::Caster * actualCaster,
	PlayerColor actionController_)
	: ProxyCaster(actualCaster)
	, actionController(actionController_)
{
}

PlayerColor ActionControllerCaster::getCasterOwner() const
{
	return actionController;
}
}
