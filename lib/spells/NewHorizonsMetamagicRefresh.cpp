/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicRefresh.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsMagic.h"
#include "HydrasVitalityIdentity.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/StackEffectRefresh.h"
#include "../battle/StackEffectRemoval.h"
#include "../networkPacks/SetStackEffect.h"
#include <algorithm>
#include <map>
#include <iterator>

namespace newHorizonsMetamagic
{
namespace
{
struct LocalReplacements
{
	std::vector<Bonus> original;
	std::map<size_t, size_t> replacementIndex;
};
bool currentPayload(const Bonus & value, SpellID accepted, SpellID family, const JsonNode & savedRules)
{
	return value.source == BonusSource::SPELL_EFFECT
		&& (value.sid == BonusSourceID(accepted) || value.sid == BonusSourceID(family))
		&& (isNumericalSpellBonus(value, accepted, family) || value.type == BonusType::HYPNOTIZED
			|| (value.type == BonusType::STACK_HEALTH && value.valType == BonusValueType::INDEPENDENT_MAX
				&& newHorizonsMagic::hydrasVitalitySource(savedRules)(&value)));
}
}

void replaceRecalculatedSpellBonuses(const CastProfile * cast, const CBattleInfoCallback & callback,
	SpellID acceptedSpell, SpellID effectSpell, const std::set<uint32_t> & preexistingRecipientIds,
	SetStackEffect & packet)
{
	// A genuine Hero context also exists without either effectiveness system.
	// Such casts keep ordinary native refresh instead of replacing magnitude.
	if(!cast || (cast->perks.rank == 0 && cast->perks.wisdomEffectivenessBonus == 0)
		|| packet.exchange || packet.toUpdate.empty())
		return;
	std::map<uint32_t, LocalReplacements> local;
	decltype(packet.toAdd) replacementAdds;
	for(auto & [unitId, incoming] : packet.toUpdate)
	{
		if(!preexistingRecipientIds.contains(unitId) || !callback.battleGetUnitByID(unitId))
			continue;
		std::vector<Bonus> retained;
		for(const auto & value : incoming)
		{
			if(!currentPayload(value, acceptedSpell, effectSpell, callback.getBattle()->getMagicRules()))
			{
				retained.push_back(value);
				continue;
			}
			auto found = local.find(unitId);
			if(found == local.end())
			{
				LocalReplacements records{callback.getBattle()->getLocalSpellEffects(unitId), {}};
				// Explicit replacement/removal at the same boundary owns its own timer.
				std::erase_if(records.original, [&](const Bonus & old)
				{
					for(const auto & [removedUnit, removed] : packet.toRemove)
						if(removedUnit == unitId)
							for(const auto & selected : removed)
								if(battle::matchesStackEffectRemoval(selected, old)) return true;
					return false;
				});
				found = local.emplace(unitId, std::move(records)).first;
			}
			bool replaced = false;
			for(size_t i = 0; i < found->second.original.size(); ++i)
			{
				const auto & old = found->second.original[i];
				if(!stackEffectRefresh::matchesIdentity(value, &old))
					continue;
				replaced = true;
				const auto scheduled = found->second.replacementIndex.find(i);
				Bonus replacement = value;
				if(scheduled == found->second.replacementIndex.end())
				{
					stackEffectRefresh::refreshFields(replacement, old);
					packet.toRemove.emplace_back(unitId, std::vector<Bonus>{old});
					found->second.replacementIndex.emplace(i, replacementAdds.size());
					replacementAdds.emplace_back(unitId, std::vector<Bonus>{std::move(replacement)});
				}
				else
				{
					auto & previous = replacementAdds[scheduled->second].second.front();
					stackEffectRefresh::refreshFields(replacement, previous);
					previous = std::move(replacement);
				}
			}
			if(!replaced)
				retained.push_back(value); // Native append/inherited-candidate semantics remain authoritative.
		}
		incoming = std::move(retained);
	}
	replacementAdds.insert(replacementAdds.end(), std::make_move_iterator(packet.toAdd.begin()),
		std::make_move_iterator(packet.toAdd.end()));
	packet.toAdd = std::move(replacementAdds);
}
}
