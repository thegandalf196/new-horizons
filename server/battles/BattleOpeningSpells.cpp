/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleFlowProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/BonusCaster.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
void BattleFlowProcessor::castOpeningSpells(const CBattleInfoCallback & battle)
{
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * h = battle.battleGetFightingHero(i);
		if (!h)
			continue;

		TConstBonusListPtr bl = h->getBonusesOfType(BonusType::OPENING_BATTLE_SPELL);

		for (const auto & b : *bl)
		{
			spells::BonusCaster caster(h, b);

			SpellID spellID = b->subtype.as<SpellID>();
			if (!spellID.hasValue())
			{
				logGlobal->error("unable to cast spell - OPENING_BATTLE_SPELL has invalid spell set!");
				continue;
			}
			const CSpell * spell = spellID.toSpell();

			spells::BattleCast parameters(&battle, &caster, spells::Mode::PASSIVE, spell);
			int32_t spellLevel = b->parameters ? b->parameters->toNumber() : 3;
			parameters.setSpellLevel(spellLevel);
			parameters.setEffectDuration(b->val);
			parameters.forceMassive = true;
			parameters.castIfPossible(gameHandler->spellcastEnvironment(), spells::Target());
		}
	}
}
