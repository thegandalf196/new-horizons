/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../nativeRules/CompiledMetamagicRules.h"
#include "NewHorizonsMetamagicArea.h"
#include "NewHorizonsMetamagicResolution.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "effects/native/StructureCommon.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <vcmi/ServerCallback.h>

namespace newHorizonsMetamagic
{
namespace
{
bool active(const spells::Mechanics & mechanics, nativeRules::MetamagicPerk perk)
{
	const auto * hero = mechanics.getHeroCaster();
	return hero && mechanics.isGenuineHeroCast() && mechanics.battle() && mechanics.battle()->getBattle()
		&& nativeRules::hasMetamagicPerk(hero, perk);
}
}

bool conventionalArea(const CSpell & spell, int32_t mastery)
{
	if(spell.getTargetType() != spells::AimType::LOCATION || spell.isPersistent())
		return false;
	const auto & rings = spell.getLevelInfo(mastery).range;
	if(rings.empty() || rings.front() < 0 || rings.back() <= 0 || rings.back() > 8)
		return false;
	for(size_t index = 1; index < rings.size(); ++index)
		if(rings[index] != rings[index - 1] + 1)
			return false;
	return true;
}

std::vector<int> widenedRings(const spells::Mechanics & mechanics, std::vector<int> rings)
{
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if((active(mechanics, nativeRules::MetamagicPerk::WidenSpell)
		|| active(mechanics, nativeRules::MetamagicPerk::WidenSpells)) && spell && !mechanics.isMassive()
		&& conventionalArea(*spell, mechanics.getRangeLevel()) && !rings.empty())
		rings.push_back(rings.back() + 1);
	return rings;
}

void breakMatter(ServerCallback & server, const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, const BattleHexArray & existingArea)
{
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if(!active(mechanics, nativeRules::MetamagicPerk::Matterbreaker) || !spell || !spell->isDamage() || mechanics.isMassive()
		|| !conventionalArea(*spell, mechanics.getRangeLevel()) || existingArea.empty())
		return;
	const JsonNode parameters;
	const CombatEventPayload payload;
	const nativeEvents::Context context{server, battle, nullptr, nullptr, parameters, payload};
	for(const auto & obstacle : battle.battleGetAllObstacles())
	{
		if(obstacle->obstacleType != CObstacleInstance::USUAL)
			continue;
		const auto footprint = obstacle->getBlockedTiles();
		if(std::any_of(footprint.begin(), footprint.end(), [&](const BattleHex & hex) { return existingArea.contains(hex); }))
			spells::effects::native::structure::removeObstacle(context, obstacle);
	}
}
}
