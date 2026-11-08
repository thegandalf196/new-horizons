/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleSpecialtyAttackControllers.h"
#include "../CGameHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/spells/AbilityCaster.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
namespace battleSpecialtyAttackControllers
{
bool apply(CGameHandler & game, const CBattleInfoCallback & battle, const newHorizonsAttackControllers::Strike & strike)
{
	if(!strike.fireball && !strike.bloodlust) return true;
	const auto battleId = battle.getBattle()->getBattleID();
	const auto * owner = game.gameState().getBattle(battleId);
	if(!owner) return false;
	const auto * source = battle.battleGetUnitByID(strike.source);
	if(!source || !source->alive() || source->isGhost() || source->isTimeStopped()) return true;
	if(strike.fireball)
	{
		const auto * hero = battle.battleGetOwnerHero(source);
		if(!hero) return true;
		newHorizonsAttackControllers::BallistaCaster caster(*source, *hero);
		spells::BattleCast cast(&battle, &caster, spells::Mode::PASSIVE, SpellID(SpellID::FIREBALL).toSpell());
		cast.castIfPossible(game.spellcastEnvironment(), spells::Target{spells::Destination(strike.impact)});
		return game.gameState().getBattle(battleId) == owner;
	}
	spells::AbilityCaster caster(source, newHorizonsAttackControllers::BLOODLUST_LEVEL);
	spells::BattleCast cast(&battle, &caster, spells::Mode::PASSIVE, SpellID(SpellID::BLOODLUST).toSpell());
	cast.setSpellLevel(newHorizonsAttackControllers::BLOODLUST_LEVEL);
	cast.setEffectDuration(newHorizonsAttackControllers::BLOODLUST_DURATION);
	const auto recipients = newHorizonsAttackControllers::bloodlustRecipients(battle, *source);
	if(recipients.empty()) return true;
	const auto choice = recipients.size() == 1 ? 0 : game.getRandomGenerator().nextInt(static_cast<int>(recipients.size()) - 1);
	const auto * target = battle.battleGetUnitByID(recipients.at(choice));
	if(target) cast.castIfPossible(game.spellcastEnvironment(), spells::Target{spells::Destination(target)});
	return game.gameState().getBattle(battleId) == owner;
}
}
