/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicEffects.h"
#include "CSpell.h"
#include "../battle/NewHorizonsElementalAttunement.h"
#include "../battle/Unit.h"
#include "../battle/CBattleInfoCallback.h"
#include "../json/JsonNode.h"
#include <cmath>
#include <stdexcept>

namespace newHorizonsMetamagic
{
int64_t outgoingDamageMagnitude(int64_t ordinary, const CastProfile * cast,
	const battle::Unit * recipient)
{
	if(!cast) return ordinary;
	Profile values = cast->perks;
	int32_t counter = recipient ? castCounter(*cast, recipient->metamagicMarks(cast->heroSide))
		: cast->perks.pointPolicy ? cast->points : 0;
	if(!recipient && !cast->perks.pointPolicy)
	{
		values = {};
		values.wisdomEffectivenessBonus = targetFreeEffectivenessBonusPercent(cast);
	}
	return spellDamageOutput(ordinary, values, counter, 0, 0, cast->factionSpellDamageBonus);
}

CastProfile capturedOffensiveProfile(const JsonNode * captured)
{
	CastProfile result;
	if(!captured || !captured->isStruct()) return result;
	const auto field = captured->Struct().find("factionSpellDamageBonusPercent");
	if(field == captured->Struct().end()) return result;
	const auto & value = field->second;
	// Native descriptor parameters preserve the former script boundary's exact
	// integral doubles. Accept those captured scalars, never fractional values.
	if(!value.isNumber() || !std::isfinite(value.Float())
		|| (value.Float() != 0 && value.Float() != 15 && value.Float() != 25 && value.Float() != 40))
		throw std::runtime_error("Invalid captured faction spell damage contribution");
	const auto applied = captured->Struct().find("factionSpellDamageCapturedInMagnitude");
	if(applied != captured->Struct().end())
	{
		if(!applied->second.isBool())
			throw std::runtime_error("Invalid captured faction damage application marker");
		if(applied->second.Bool()) return result;
	}
	result.factionSpellDamageBonus = static_cast<int32_t>(value.Integer());
	return result;
}

int64_t deferredSpellDamage(int64_t ordinary, const JsonNode * captured,
	const CBattleInfoCallback & battle, const battle::Unit * recipient, PlayerColor spellOwner)
{
	auto profile = capturedOffensiveProfile(captured);
	if(profile.factionSpellDamageBonus == 0)
		return spellDamage(ordinary, nullptr, battle, recipient, spellOwner);
	profile.heroSide = battle.playerToSide(spellOwner);
	if(profile.heroSide != BattleSide::ATTACKER && profile.heroSide != BattleSide::DEFENDER)
		throw std::runtime_error("Captured offensive spell damage requires an owning hero side");
	return spellDamage(ordinary, &profile, battle, recipient, spellOwner);
}

int64_t factionSpellDamagePreview(int64_t ordinary,
	const CGHeroInstance & hero, const CSpell & spell, const JsonNode & rules)
{
	CastProfile cast;
	cast.factionSpellDamageBonus = newHorizonsElementalAttunement::spellDamageBonusPercent(
		newHorizonsElementalAttunement::captureProfile(&hero), rules, spell.getId());
	return outgoingDamageMagnitude(ordinary, &cast, nullptr);
}
}
