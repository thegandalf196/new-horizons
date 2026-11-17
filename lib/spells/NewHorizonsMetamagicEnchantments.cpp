/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicEnchantments.h"
#include "CSpell.h"
#include "../nativeRules/CompiledMetamagicRules.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/Unit.h"
#include "../networkPacks/SetStackEffect.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMetamagic
{
void enchantmentCapabilities(const CGHeroInstance * hero, const CBattleInfoCallback & callback,
	SpellID accepted, SpellID family, SetStackEffect & packet)
{
	const auto * spell = accepted.toSpell();
	if(!hero || !spell || !spell->isPositive() || !spell->isMagical() || packet.exchange) return;
	const bool flying = nativeRules::hasMetamagicPerk(hero, nativeRules::MetamagicPerk::Levitation);
	const bool shooting = nativeRules::hasMetamagicPerk(hero, nativeRules::MetamagicPerk::ArcaneBallistics);
	if(!flying && !shooting) return;
	const auto append = [&](auto & changes)
	{
		for(auto & [id, bonuses] : changes)
		{
			const auto * unit = callback.battleGetUnitByID(id);
			if(!unit || !unit->alive() || callback.battleGetOwner(unit) != hero->getOwner()) continue;
			const auto seed = std::ranges::find_if(bonuses, [&](const Bonus & bonus)
			{
				return (!bonus.abilityDescriptor || bonus.abilityDescriptor->family == creatureAbility::Family::HERO_SPELL
					|| bonus.abilityDescriptor->family == creatureAbility::Family::CREATURE_SPELL)
					&& bonus.source == BonusSource::SPELL_EFFECT
					&& (bonus.sid == BonusSourceID(accepted) || bonus.sid == BonusSourceID(family))
					&& bonus.duration != BonusDuration::PERMANENT
					&& ((bonus.duration & BonusDuration::N_TURNS) == 0 || bonus.turnsRemain > 0);
			});
			if(seed == bonuses.end()) continue;
			const Bonus original = *seed;
			for(const auto [type, enabled] : {std::pair{BonusType::FLYING, flying}, std::pair{BonusType::FREE_SHOOTING, shooting}})
			{
				if(!enabled || std::ranges::any_of(bonuses, [&](const Bonus & bonus) { return bonus.type == type; })) continue;
				auto capability = original;
				capability.type = type;
				capability.subtype = BonusSubtypeID();
				capability.val = 1;
				capability.valType = BonusValueType::ADDITIVE_VALUE;
				capability.parameters.reset();
				capability.limiter.reset();
				capability.updater.reset();
				capability.propagator.reset();
				capability.propagationUpdater.reset();
				bonuses.push_back(std::move(capability));
			}
		}
	};
	append(packet.toAdd);
	append(packet.toUpdate);
}
}
