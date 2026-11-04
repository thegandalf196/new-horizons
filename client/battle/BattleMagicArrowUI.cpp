/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInterface.h"
#include "BattleActionsController.h"
#include "MagicArrowOverchargeWindow.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/effects/Effect.h"

namespace
{
std::optional<spells::effects::SpellEffectValue> previewArrowChange(
	const CBattleInfoCallback & battle, const CGHeroInstance & hero, const CSpell & spell,
	const battle::Unit & target, bool followup, int selectedOvercharge)
{
	spells::BattleCast preview(&battle, &hero, spells::Mode::HERO, &spell);
	preview.setMetamagicFollowup(followup);
	preview.setMetamagicTargetUnitId(target.unitId());
	preview.setOvercharge(selectedOvercharge);
	auto mechanics = spell.battleMechanics(&preview);
	if(!mechanics)
		return std::nullopt;

	spells::Target aim;
	aim.emplace_back(target.getPosition());
	aim.emplace_back(spells::Destination(&target, target.getPosition()));
	const auto canonicalTarget = mechanics->canonicalizeTarget(aim);
	std::optional<spells::effects::SpellEffectValue> result;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		// Magic Arrow currently has one directDamage effect. Stop at that
		// effect so adding another spell effect cannot double-count this
		// single-hit preview.
		if(effect.name != "directDamage")
			return false;

		const auto effectTarget = effect.transformTarget(mechanics.get(), aim, canonicalTarget);
		result = effect.getHealthChange(mechanics.get(), effectTarget);
		return true;
	});
	return result;
}

int targetManaCost(const CBattleInfoCallback & battle, const CGHeroInstance & hero,
	const CSpell & spell, const battle::Unit & target, bool followup, int overcharge)
{
	spells::BattleCast cast(&battle, &hero, spells::Mode::HERO, &spell);
	cast.setMetamagicFollowup(followup);
	cast.setMetamagicTargetUnitId(target.unitId());
	cast.setOvercharge(overcharge);
	const auto mechanics = spell.battleMechanics(&cast);
	spells::Target aim;
	aim.emplace_back(&target, target.getPosition());
	return mechanics->getTargetAwareManaCost(aim);
}

int maximumTargetOvercharge(const CBattleInfoCallback & battle, const CGHeroInstance & hero,
	const CSpell & spell, const battle::Unit & target, bool followup, int ordinaryBase, int formulaMaximum)
{
	spells::BattleCast cast(&battle, &hero, spells::Mode::HERO, &spell);
	cast.setMetamagicFollowup(followup);
	const auto mechanics = spell.battleMechanics(&cast);
	const auto * profile = mechanics->getMetamagicCastProfile();
	if(!profile)
		return static_cast<int>(std::min<int64_t>(formulaMaximum,
			std::max<int64_t>(0, hero.getManaAvailable() - ordinaryBase)));
	const std::array<uint32_t, 1> recipients{target.unitId()};
	const auto counts = newHorizonsMetamagic::manaTargetCounts(profile, battle, recipients);
	return std::max(0, newHorizonsMetamagic::maximumAffordableOvercharge(
		hero.getManaAvailable(), ordinaryBase, formulaMaximum, profile->perks, counts.qualifying, counts.affected,
		profile->singularityReached));
}
}

void BattleInterface::installMagicArrowOverchargeUI()
{
	if(!actionsController)
		return;

	actionsController->setMagicArrowOverchargeFactory(
		[this](const BattleAction & pending, const BattleHex & targetHex, const CStack * initialTarget)
			-> std::optional<MagicArrowOverchargeContext>
		{
			if(!curInt || !curInt->cb || !initialTarget)
				return std::nullopt;

			const auto callback = curInt->cb->getBattle(getBattleID());
			if(!callback || !callback->getBattle())
				return std::nullopt;

			const auto * spell = pending.spell.toSpell();
			const auto * hero = currentHero();
			if(!spell || spell->id != SpellID(SpellID::MAGIC_ARROW) || !hero
				|| !newHorizonsMagic::magicArrowOverchargeEnabled(callback->getBattle()->getMagicRules(), spell->id))
				return std::nullopt;

			const uint32_t targetUnitID = initialTarget->unitId();
			const BattleID localBattleID = getBattleID();
			const int32_t spellPower = hero->getEffectPower(spell);
			const int formulaMaximumOvercharge = newHorizonsMagic::magicArrowMaxOvercharge(
				callback->getBattle()->getMagicRules(), spell->id, spellPower,
				newHorizonsMagic::magicArrowOverchargeModifiers(hero));
			const bool metamagicFollowup = pending.metamagicFollowup;
			const int baseMana = callback->battleGetSpellCostBreakdown(spell, hero, 1, metamagicFollowup).finalCost;
			const int maximumOvercharge = maximumTargetOvercharge(*callback, *hero, *spell,
				*initialTarget, metamagicFollowup, baseMana, formulaMaximumOvercharge);

			// The shared helper discounts the complete cost, including Overcharge.
			// Its affordability search performs only bounded scalar arithmetic.
			const auto evaluate = [this, localBattleID, targetUnitID, spell, maximumOvercharge,
				metamagicFollowup]
				(int overcharge)
				-> MagicArrowOverchargeValues
			{
				MagicArrowOverchargeValues values;
				values.maximumOvercharge = maximumOvercharge;
				values.overcharge = std::clamp(overcharge, 0, maximumOvercharge);

				if(!curInt || !curInt->cb || curInt->cb->getBattle(localBattleID) == nullptr)
					return values;
				const auto callback = curInt->cb->getBattle(localBattleID);
				const auto * hero = currentHero();
				if(!callback || !callback->getBattle() || !hero)
					return values;
				const auto * target = callback->battleGetUnitByID(targetUnitID);
				if(!target)
					return values;

				values.baseMana = targetManaCost(*callback, *hero, *spell, *target, metamagicFollowup, 0);
				values.totalMana = targetManaCost(*callback, *hero, *spell, *target, metamagicFollowup, values.overcharge);
				values.additionalMana = values.totalMana - values.baseMana;
				values.availableMana = hero->getManaAvailable();
				values.affordable = values.totalMana <= values.availableMana;
				values.targetDescription = "Target: " + std::to_string(target->getCount()) + " "
					+ target->unitType()->getNamePluralTranslated();
				values.magicResistancePercent = std::clamp(target->magicResistance(), 0, 100);

				spells::BattleCast legality(callback.get(), hero, spells::Mode::HERO, spell);
				legality.setMetamagicFollowup(metamagicFollowup);
				legality.setMetamagicTargetUnitId(targetUnitID);
				legality.setOvercharge(values.overcharge);
				spells::detail::ProblemImpl legalityProblem;
				battle::Target targetCheck;
				targetCheck.emplace_back(target, target->getPosition());
				values.legal = spell->battleMechanics(&legality)->canBeCastAt(targetCheck, legalityProblem);


				const auto baseChange = previewArrowChange(*callback, *hero, *spell, *target, metamagicFollowup, 0);
				const auto selectedChange = previewArrowChange(*callback, *hero, *spell, *target, metamagicFollowup, values.overcharge);
				if(!baseChange || !selectedChange)
					return values;
				values.previewAvailable = true;
				values.baseDamage = std::max<int64_t>(0, -baseChange->hpDelta);
				values.projectedDamage = std::max<int64_t>(0, -selectedChange->hpDelta);
				values.baseKills = static_cast<int>(std::clamp<int64_t>(-baseChange->unitsDelta, 0, target->getCount()));
				values.projectedKills = static_cast<int>(std::clamp<int64_t>(-selectedChange->unitsDelta, 0, target->getCount()));
				return values;
			};

			MagicArrowOverchargeContext context;
			context.anchor = ENGINE->getCursorPosition();
			context.initial = evaluate(0);
			context.evaluate = evaluate;
			context.confirm = [this, pending, localBattleID, targetUnitID, spell, formulaMaximumOvercharge,
				metamagicFollowup](int overcharge)
				-> bool
			{
				if(!curInt || !curInt->cb || curInt->cb->getBattle(localBattleID) == nullptr)
					return false;
				const auto callback = curInt->cb->getBattle(localBattleID);
				const auto * hero = currentHero();
				if(!callback || !callback->getBattle() || !hero || overcharge < 0 || overcharge > formulaMaximumOvercharge
					|| !newHorizonsMagic::magicArrowOverchargeEnabled(callback->getBattle()->getMagicRules(), spell->id))
					return false;
				const auto * target = callback->battleGetUnitByID(targetUnitID);
				if(!target)
					return false;

				const int totalCost = targetManaCost(*callback, *hero, *spell, *target, metamagicFollowup, overcharge);
				if(totalCost < 0 || totalCost > hero->getManaAvailable())
					return false;

				BattleAction action = pending;
				action.target.clear();
				action.aimToUnit(target);
				action.spellOvercharge = overcharge;
				curInt->cb->battleMakeSpellAction(localBattleID, action);
				if(actionsController)
					actionsController->endCastingSpell();
				return true;
			};
			context.cancel = [this]
			{
				if(actionsController)
					actionsController->endCastingSpell();
			};
			return context;
		});
}
