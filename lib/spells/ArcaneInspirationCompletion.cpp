/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArcaneInspirationCompletion.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "../battle/ArcaneInspiration.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/NewHorizonsCombatSkills.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusList.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>

namespace arcaneInspiration
{
void publishCompletedSpell(ServerCallback & server, const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, bool negated, std::span<const BattleStackAttacked> originalInjuries,
	std::optional<BattleSide> originalHeroSide)
{
	if(negated || !battle.getBattle() || (!originalHeroSide && !mechanics.isGenuineHeroCast())
		|| !newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules()))
		return;
	publishCompletedSpell(server, battle, originalHeroSide.value_or(mechanics.getCasterSide()), originalInjuries);
}
void publishCompletedSpell(ServerCallback & server, const CBattleInfoCallback & battle,
	BattleSide side, std::span<const BattleStackAttacked> originalInjuries)
{
	if(!battle.getBattle() || !newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules()))
		return;
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return;
	const auto policy = profile(battle.battleGetFightingHero(side));
	if(policy.corePercent == 0)
		return;
	const auto owner = battle.sideToPlayer(side);
	const auto round = battle.battleGetRound();
	SetStackEffect packet;
	packet.battleID = battle.getBattle()->getBattleID();
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || unit->isGhost() || battle.battleGetOwner(unit) != owner
			|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit))
			continue;
		std::vector<Bonus> previous;
		for(const auto & effect : *unit->getBonuses(CSelector(isEffect)))
			if(!isSpellshock(effect.get()) && effect->spellCasterOwner == owner)
				previous.push_back(*effect);
		if(!previous.empty())
			packet.toRemove.emplace_back(unit->unitId(), std::move(previous));
		const auto active = activeMarker(policy, owner, round);
		std::vector<Bonus> effects{active};
		if(policy.riders & LINGERING)
			effects.push_back(retainedMarker(active));
		if(policy.riders & VALOR)
			effects.push_back(valorMarker(owner, round));
		packet.toAdd.emplace_back(unit->unitId(), std::move(effects));
	}

	if(!packet.toAdd.empty() || !packet.toRemove.empty())
	{
		server.apply(packet);
		server.recordArcaneInspirationEffects(packet);
	}
	std::vector<BattleStackAttacked> direct;
	for(const auto & injury : originalInjuries)
		if(!(injury.flags & BattleStackAttacked::SECONDARY)) direct.push_back(injury);
	publishSpellDamage(server, battle, side, direct);
}
}
