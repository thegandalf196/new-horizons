/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMagicalMemory.h"
#include "../nativeRules/CompiledMetamagicRules.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>

namespace newHorizonsMetamagic
{
namespace
{
constexpr const char * identity = "new-horizons:metamagic.magicalMemory";
// Raw exported helpers are compatibility boundaries; live packets use the
// battle-owned typed table below and never resolve identities per bonus.
BonusSourceID rawSource()
{
	return BonusSourceID(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")));
}

bool marker(const Bonus & bonus, BonusSourceID source)
{
	return bonus.type == BonusType::NONE && bonus.source == BonusSource::SECONDARY_SKILL
		&& bonus.sid == source && bonus.stacking == identity && bonus.duration == BonusDuration::ONE_BATTLE
		&& bonus.subtype.as<SpellID>().hasValue() && (bonus.val == 1 || bonus.val == 2);
}

Bonus makeMarker(SpellID family, ResolutionKind removedKind, BonusSourceID source)
{
	if(!family.hasValue())
		throw std::invalid_argument("Magical Memory requires an exact spell family");
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SECONDARY_SKILL,
		removedKind == ResolutionKind::OFFENSE ? 1 : 2, source, BonusSubtypeID(family));
	result.stacking = identity;
	result.hidden = true;
	return result;
}

bool blocks(const battle::Unit & unit, const nativeRules::CompiledMetamagicEnergy & table,
	SpellID spell, bool positive, bool negative)
{
	const auto * spellIdentity = table.identity(spell);
	const auto skill = table.memorySkill();
	if(!spellIdentity || !skill || (!positive && !negative))
		return false;
	const auto bonuses = unit.getBonuses(Selector::type()(BonusType::NONE));
	for(const auto & bonus : *bonuses)
		if(marker(*bonus, BonusSourceID(*skill)) && bonus->subtype == BonusSubtypeID(spellIdentity->family)
			&& ((positive && bonus->val == 1) || (negative && bonus->val == 2)))
			return true;
	return false;
}
}

bool isMagicalMemory(const Bonus & bonus)
{
	return marker(bonus, rawSource());
}

Bonus magicalMemoryMarker(SpellID family, ResolutionKind removedKind)
{
	return makeMarker(family, removedKind, rawSource());
}

bool blocksEnchantment(const battle::Unit & unit, const JsonNode & rules, SpellID spell, bool positive, bool negative)
{
	const auto table = nativeRules::CompiledMetamagicEnergy::compileRawBoundary(rules);
	return blocks(unit, *table, spell, positive, negative);
}

bool blocksEnchantment(const battle::Unit & unit, const IBattleInfo & battle, SpellID spell, bool positive, bool negative)
{
	const auto table = nativeRules::captureMetamagicEnergy(battle);
	return blocks(unit, *table, spell, positive, negative);
}

void filterNewBonuses(const CBattleInfoCallback & battle, SetStackEffect & packet)
{
	if(!battle.getBattle())
		return;
	const auto table = nativeRules::captureMetamagicEnergy(*battle.getBattle());
	const auto filter = [&](auto & changes)
	{
		for(auto & [id, bonuses] : changes)
			if(const auto * unit = battle.battleGetUnitByID(id))
				std::erase_if(bonuses, [&](const Bonus & bonus)
				{
					if(bonus.source != BonusSource::SPELL_EFFECT)
						return false;
					const auto spell = bonus.sid.as<SpellID>();
					const auto * data = table->identity(spell);
					return data && blocks(*unit, *table, spell, data->positive, data->negative);
				});
		std::erase_if(changes, [](const auto & change) { return change.second.empty(); });
	};
	filter(packet.toAdd);
	filter(packet.toUpdate);
}

void rememberRemoval(ServerCallback & server, const CBattleInfoCallback & battle,
	uint32_t recipient, SpellID family, ResolutionKind removedKind)
{
	const auto * unit = battle.battleGetUnitByID(recipient);
	if(!unit || !battle.getBattle())
		return;
	const auto table = nativeRules::captureMetamagicEnergy(*battle.getBattle());
	const auto skill = table->memorySkill();
	if(!skill || !table->identity(family) || blocks(*unit, *table, family,
		removedKind == ResolutionKind::OFFENSE, removedKind == ResolutionKind::RESTORATION))
		return;
	SetStackEffect packet;
	packet.battleID = battle.getBattle()->getBattleID();
	packet.toAdd.emplace_back(recipient, std::vector<Bonus>{makeMarker(family, removedKind, BonusSourceID(*skill))});
	server.apply(packet);
}
}
