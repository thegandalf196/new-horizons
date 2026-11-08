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
		if(effect.name != "directDamage")
			return false;
		const auto effectTarget = effect.transformTarget(mechanics.get(), aim, canonicalTarget);
		result = effect.getHealthChange(mechanics.get(), effectTarget);
		return true;
	});
	return result;
}

struct ArrowSelection
{
	std::weak_ptr<BattleInterface> owner;
	BattleID battleId;
	ObjectInstanceID heroId;
	uint32_t targetUnitId;
	SpellID spellId;
	bool metamagicFollowup;
	uint64_t castingSession;

	struct Current
	{
		std::shared_ptr<BattleInterface> owner;
		std::shared_ptr<CPlayerBattleCallback> callback;
		const CGHeroInstance * hero;
		const battle::Unit * target;
		const CSpell * spell;
	};

	std::optional<Current> current() const
	{
		const auto active = owner.lock();
		if(!active || CPlayerInterface::battleInt != active || !active->curInt
			|| active->getBattleID() != battleId || !active->curInt->cb || !active->actionsController
			|| active->actionsController->getCastingSession() != castingSession
			|| !active->actionsController->heroSpellcastingModeActive())
			return std::nullopt;
		const auto callback = active->curInt->cb->getBattle(battleId);
		const auto * hero = active->currentHero();
		const auto * spell = spellId.toSpell();
		if(!callback || !callback->getBattle() || !hero || hero->id != heroId
			|| !spell || spellId != SpellID(SpellID::MAGIC_ARROW)
			|| !newHorizonsMagic::magicArrowOverchargeEnabled(callback->getBattle()->getMagicRules(), spellId))
			return std::nullopt;
		const auto * target = callback->battleGetUnitByID(targetUnitId);
		if(!target || !target->alive())
			return std::nullopt;
		return Current{active, callback, hero, target, spell};
	}

	MagicArrowOverchargeValues evaluate(int overcharge) const
	{
		MagicArrowOverchargeValues values;
		values.overcharge = overcharge;
		values.legal = false;
		values.unavailableReason = "Casting context changed. Cancel and choose again.";
		const auto state = current();
		if(!state)
			return values;
		const auto & [active, callback, hero, target, spell] = *state;
		const auto & rules = callback->getBattle()->getMagicRules();
		const int powerForCap = hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
		values.maximumOvercharge = newHorizonsMagic::magicArrowMaxOvercharge(rules, spellId,
			powerForCap, newHorizonsMagic::magicArrowOverchargeModifiers(hero));
		values.availableMana = hero->getManaAvailable();
		values.targetDescription = "Target: " + std::to_string(target->getCount()) + " "
			+ target->unitType()->getNamePluralTranslated();
		values.magicResistancePercent = std::clamp(target->magicResistance(), 0, 100);
		if(overcharge < 0 || overcharge > values.maximumOvercharge)
		{
			values.unavailableReason = "This Overcharge level is no longer legal.";
			return values;
		}

		spells::BattleCast legality(callback.get(), hero, spells::Mode::HERO, spell);
		legality.setMetamagicFollowup(metamagicFollowup);
		legality.setMetamagicTargetUnitId(targetUnitId);
		legality.setOvercharge(overcharge);
		const auto mechanics = spell->battleMechanics(&legality);
		battle::Target targetCheck;
		targetCheck.emplace_back(target, target->getPosition());
		spells::detail::ProblemImpl legalityProblem;
		values.legal = mechanics->canBeCast(legalityProblem)
			&& mechanics->canBeCastAt(targetCheck, legalityProblem);
		values.totalMana = mechanics->getTargetAwareManaCost(targetCheck);
		legality.setOvercharge(0);
		values.baseMana = spell->battleMechanics(&legality)->getTargetAwareManaCost(targetCheck);
		values.costAvailable = true;
		values.additionalMana = values.totalMana - values.baseMana;
		values.affordable = values.totalMana >= 0 && values.totalMana <= values.availableMana;
		values.unavailableReason = !values.affordable ? "Not enough Mana."
			: !values.legal ? "Casting is no longer available. Cancel and choose again." : "";

		const auto baseChange = previewArrowChange(*callback, *hero, *spell, *target, metamagicFollowup, 0);
		const auto selectedChange = previewArrowChange(*callback, *hero, *spell, *target, metamagicFollowup, overcharge);
		if(!baseChange || !selectedChange)
			return values;
		values.previewAvailable = true;
		values.baseDamage = std::max<int64_t>(0, -baseChange->hpDelta);
		values.projectedDamage = std::max<int64_t>(0, -selectedChange->hpDelta);
		values.baseKills = static_cast<int>(std::clamp<int64_t>(-baseChange->unitsDelta, 0, target->getCount()));
		values.projectedKills = static_cast<int>(std::clamp<int64_t>(-selectedChange->unitsDelta, 0, target->getCount()));
		return values;
	}

	bool confirm(const BattleAction & pending, int overcharge) const
	{
		const auto state = current();
		const auto values = evaluate(overcharge);
		if(!state || !values.legal || !values.affordable)
			return false;
		BattleAction action = pending;
		action.target.clear();
		action.aimToUnit(state->target);
		action.spellOvercharge = overcharge;
		state->owner->curInt->cb->battleMakeSpellAction(battleId, action);
		if(state->owner->actionsController)
			state->owner->actionsController->endCastingSpell();
		return true;
	}
};
}

void BattleInterface::installMagicArrowOverchargeUI()
{
	if(!actionsController)
		return;
	actionsController->setMagicArrowOverchargeFactory(
		[this](const BattleAction & pending, const BattleHex &, const CStack * initialTarget)
			-> std::optional<MagicArrowOverchargeContext>
		{
			const auto * hero = currentHero();
			if(!hero || !initialTarget || !curInt || !curInt->cb)
				return std::nullopt;
			const ArrowSelection selection{CPlayerInterface::battleInt, getBattleID(), hero->id,
				initialTarget->unitId(), pending.spell, pending.metamagicFollowup, actionsController->getCastingSession()};
			if(!selection.current())
				return std::nullopt;
			MagicArrowOverchargeContext context;
			context.anchor = ENGINE->getCursorPosition();
			context.initial = selection.evaluate(0);
			context.evaluate = [selection](int overcharge) { return selection.evaluate(overcharge); };
			context.confirm = [selection, pending](int overcharge) { return selection.confirm(pending, overcharge); };
			context.cancel = [selection]
			{
				if(const auto active = selection.owner.lock(); active && CPlayerInterface::battleInt == active
					&& active->getBattleID() == selection.battleId && active->currentHero()
					&& active->currentHero()->id == selection.heroId && active->actionsController
					&& active->actionsController->getCastingSession() == selection.castingSession)
					active->actionsController->endCastingSpell();
			};
			return context;
		});
}
