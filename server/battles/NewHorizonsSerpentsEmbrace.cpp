/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "../CGameHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace newHorizonsSerpentsEmbrace
{
bool rejectsVoluntaryMovement(const CBattleInfoCallback & battle, const BattleAction & action,
	const battle::Unit * source)
{
	if(!source || (action.actionType != EActionType::WALK
		&& action.actionType != EActionType::WALK_AND_ATTACK && action.actionType != EActionType::WALK_AND_CAST)
		|| !source->hasBonusOfType(BonusType::BIND_EFFECT) || !blocksVoluntaryMovement(*source))
		return false;
	const auto target = action.getTarget(&battle);
	return !target.empty() && target.front().hexValue != source->getPosition();
}
std::optional<PreparedBinding> prepareBinding(const CBattleInfoCallback & battle,
	const battle::Unit * source, const battle::Unit * primary, bool ranged, bool physical,
	bool secondary, const std::vector<BattleStackAttacked> & hits)
{
	if(!qualifiesAttack(battle, source, primary, ranged, physical, secondary))
		return std::nullopt;
	const auto hit = std::ranges::find(hits, primary->unitId(), &BattleStackAttacked::stackAttacked);
	if(hit == hits.end() || hit->damageAmount <= 0)
		return std::nullopt;
	return PreparedBinding{source->unitId(), primary->unitId(), marker(source->unitId(), source->creatureId())};
}
void applyBinding(CGameHandler & handler, const CBattleInfoCallback & battle,
	const std::optional<PreparedBinding> & binding)
{
	if(!binding)
		return;
	const auto * source = battle.battleGetUnitByID(binding->source);
	const auto * target = battle.battleGetUnitByID(binding->target);
	if(!qualifiesAttack(battle, source, target, false, true, false))
		return;
	// There is no potency or round timer to refresh before this source next acts.
	if(!releasePlan(*target, binding->source).empty())
		return;
	SetStackEffect effect;
	effect.battleID = battle.getBattle()->getBattleID();
	effect.toAdd.emplace_back(binding->target, std::vector<Bonus>{binding->restraint});
	handler.sendAndApply(effect);
	BattleLogMessage feedback;
	feedback.battleID = effect.battleID;
	MetaString line;
	line.appendRawString("Serpent's Embrace from ");
	line.appendRawString(source->getDescription());
	line.appendRawString(" binds ");
	line.appendRawString(target->getDescription());
	line.appendRawString(" until the source Naga's next activation or death.");
	feedback.lines.push_back(std::move(line));
	handler.sendAndApply(feedback);
}
}
