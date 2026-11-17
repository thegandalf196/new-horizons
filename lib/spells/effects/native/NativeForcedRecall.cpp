/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../NewHorizonsSorcerySpells.h"
#include "../../ISpellMechanics.h"
#include "../../../battle/BattleEffectExchange.h"
#include "../../../battle/IBattleState.h"
#include "../../../networkPacks/SetStackEffect.h"

namespace spells::effects::native
{
namespace
{
class ForcedRecall final : public UnitEffect
{
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		return unit && mechanics->ownerMatches(unit)
			&& newHorizonsSorcerySpells::forcedRecallDestination(*mechanics->battle(), *unit).has_value();
	}
public:
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		auto result = UnitEffect::transformTarget(mechanics, aim, target);
		if(result.size() > 1) result.resize(1);
		return result;
	}
	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & target) const override
	{
		if(target.size() != 1 || !target.front().unitValue) return;
		const auto & unit = *target.front().unitValue;
		if(const auto landing = newHorizonsSorcerySpells::forcedRecallDestination(*mechanics->battle(), unit))
			hexes.insert(unit.getHexes(*landing));
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(target.size() != 1 || !isValidTarget(mechanics, target.front().unitValue)
			|| !isReceptive(mechanics, target.front().unitValue)) return;
		const auto & unit = *target.front().unitValue;
		const auto landing = newHorizonsSorcerySpells::forcedRecallDestination(*mechanics->battle(), unit);
		if(!landing) return;
		std::vector<Bonus> roots;
		for(const auto & bonus : mechanics->battle()->getBattle()->getLocalSpellEffects(unit.unitId()))
			if(bonus.type == BonusType::BIND_EFFECT && bonus.source == BonusSource::SPELL_EFFECT)
				roots.push_back(bonus);
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		nativeEvents::moveUnit(context, unit, *landing, true);
		const auto * moved = mechanics->battle()->battleGetUnitByID(unit.unitId());
		if(moved && moved->alive() && !moved->isGhost())
		{
			const auto after = mechanics->battle()->getBattle()->getLocalSpellEffects(unit.unitId());
			std::erase_if(roots, [&](const Bonus & old)
				{ return std::ranges::any_of(after, [&](const Bonus & current)
					{ return battle::exactBattleEffectEqual(old, current); }); });
			if(!roots.empty())
			{
				SetStackEffect preserved;
				preserved.battleID = mechanics->getBattleID();
				preserved.toAdd.emplace_back(unit.unitId(), std::move(roots));
				server->apply(preserved);
			}
		}
		server->recordResolvedSpellRecipient(unit.unitId());
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createForcedRecallEffect(const std::string & path)
{
	return path == "spells/forcedRecall" ? std::make_shared<spells::effects::native::ForcedRecall>() : nullptr;
}
}
