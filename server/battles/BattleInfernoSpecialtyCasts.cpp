/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfernoSpecialtyCasts.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/hero/NewHorizonsInfernoSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"

namespace battleInfernoSpecialties
{
void castMarkedForTheKill(CGameHandler & handler, const CBattleInfoCallback & battle)
{
	const auto frailty = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell", "new-horizons:frailty", false);
	if(!frailty) return;
	const auto * spell = SpellID(*frailty).toSpell();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battle.battleGetFightingHero(side);
		const int count = newHorizonsHeroes::markedForTheKillStacks(hero);
		if(count <= 0) continue;
		spells::BattleCast opening(&battle, hero, spells::Mode::PASSIVE, spell);
		opening.setSpellLevel(0);
		auto mechanics = spell->battleMechanics(&opening);
		auto targets = battle.battleGetUnitsIf([&](const battle::Unit * unit)
		{
			return unit->alive() && !unit->isGhost() && !unit->isTurret()
				&& battle.battleGetOwner(unit) != hero->getOwner()
				&& mechanics->isReceptive(unit)
				&& mechanics->canBeCastAt(spells::Target{spells::Destination(unit)});
		});
		std::stable_sort(targets.begin(), targets.end(), [](const auto * left, const auto * right)
		{
			const int64_t leftValue = static_cast<int64_t>(left->unitType()->getAIValue()) * left->getCount();
			const int64_t rightValue = static_cast<int64_t>(right->unitType()->getAIValue()) * right->getCount();
			return leftValue != rightValue ? leftValue > rightValue : left->unitId() < right->unitId();
		});
		int attempted = 0;
		for(const auto * unit : targets)
		{
			if(attempted++ >= count) break;
			spells::BattleCast cast(&battle, hero, spells::Mode::PASSIVE, spell);
			cast.setSpellLevel(0);
			cast.forceMassive = false;
			cast.castIfPossible(handler.spellcastEnvironment(), spells::Target{spells::Destination(unit)});
		}
	}
}
}
