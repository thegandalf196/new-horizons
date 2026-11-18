/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OverflowSpellPresentation.h"
#include "BattleAnimationClasses.h"
#include "BattleInterface.h"
#include "BattleProjectileController.h"
#include "BattleStacksController.h"
#include "CreatureAnimation.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/CSpell.h"

namespace
{
/// Ordinary projectiles already protect first render and own their elapsed time.
/// This bridge only waits for arrival before running the existing injury feedback.
class OverflowFlightAnimation final : public BattleAnimation
{
	Point origin;
	Point destination;
	const CSpell * spell;
	std::function<void()> onArrival;

	bool init() override
	{
		owner.projectilesController->createSpellProjectile(nullptr, origin, destination, spell);
		owner.projectilesController->emitStackProjectile(nullptr);
		return true;
	}

	void tick(uint32_t) override
	{
		// Cascade launches in one stage; both injuries remain simultaneous after
		// all of that stage's missiles arrive, as in the authoritative receipt.
		if(owner.projectilesController->hasActiveProjectile(nullptr, false))
			return;
		onArrival();
		delete this;
	}

public:
	OverflowFlightAnimation(BattleInterface & owner, Point origin, Point destination,
		const CSpell * spell, std::function<void()> onArrival)
		: BattleAnimation(owner), origin(origin), destination(destination), spell(spell),
		  onArrival(std::move(onArrival))
	{
	}
};
}

void OverflowSpellPresentation::reset()
{
	manualSpell = SpellID::NONE;
	manualSide = BattleSide::NONE;
	primaryHex = BattleHex::INVALID;
}

void OverflowSpellPresentation::begin(const BattleAction & action)
{
	reset();
	if(action.actionType == EActionType::HERO_SPELL)
	{
		manualSpell = action.spell;
		manualSide = action.side;
	}
}

void OverflowSpellPresentation::remember(BattleInterface & owner, const BattleSpellCast & receipt)
{
	// Passive effects, creature casts and reflections cannot replace the original
	// manual cast's origin while its injuries are still being presented.
	if(!receipt.activeCast || !receipt.castByHero || receipt.casterStack >= 0
		|| receipt.spellID != manualSpell || receipt.side != manualSide || !receipt.tile.isValid()
		|| primaryHex.isValid())
		return;

	primaryHex = receipt.tile;
	if(receipt.affectedCres.size() == 1)
		if(const auto * primary = owner.getBattle()->battleGetStackByID(receipt.affectedCres.front(), false);
			primary && primary->coversPos(receipt.tile))
			primaryHex = primary->getPosition();
}

bool OverflowSpellPresentation::queue(BattleInterface & owner, const StackAttackedInfo & injury,
	std::function<void()> onArrival) const
{
	if(!injury.replaySpellHit || injury.spellEffect != manualSpell || !primaryHex.isValid()
		|| !injury.defender || !injury.defender->getPosition().isValid())
		return false;
	const auto * spell = injury.spellEffect.toSpell();
	if(!spell || spell->animationInfo.projectile.empty())
		return false;

	const auto from = owner.stacksController->getStackPositionAtHex(primaryHex, nullptr) + Point(225, 225);
	const auto to = owner.stacksController->getStackPositionAtHex(injury.defender->getPosition(), injury.defender) + Point(225, 225);
	owner.addToAnimationStage(EAnimationEvents::SPELL_OVERFLOW, [&owner, from, to, spell, onArrival = std::move(onArrival)]()
	{
		owner.stacksController->addNewAnim(new OverflowFlightAnimation(owner, from, to, spell, onArrival));
	});
	return true;
}

void BattleStacksController::queueInjuryAnimation(const StackAttackedInfo & attackedInfo)
{
	bool useDeathAnim   = attackedInfo.killed;
	bool useDefenceAnim = attackedInfo.defender->defending && !attackedInfo.indirectAttack && !attackedInfo.killed;

	EAnimationEvents usedEvent = useDefenceAnim ? EAnimationEvents::ATTACK : EAnimationEvents::HIT;

	auto showInjury = [this, attackedInfo, useDeathAnim, useDefenceAnim]()
	{
		unlockStackAmountBox(attackedInfo.defender->unitId());
		if(attackedInfo.shattered)
			frozenShatterFeedback[attackedInfo.defender->unitId()] = {
				stackAnimation.at(attackedInfo.defender->unitId())->currentFrameContentRect(facingRight(attackedInfo.defender)), 0};

		if (useDeathAnim)
			addNewAnim(new DeathAnimation(owner, attackedInfo.defender, attackedInfo.indirectAttack));
		else if(useDefenceAnim)
			addNewAnim(new DefenceAnimation(owner, attackedInfo.defender));
		else
			addNewAnim(new HittedAnimation(owner, attackedInfo.defender));

		if (attackedInfo.spellEffect != SpellID::NONE)
			owner.displaySpellInjury(attackedInfo.spellEffect.toSpell(),
				attackedInfo.defender->getPosition(), attackedInfo.replaySpellHit);
	};
	if(!owner.overflowSpellPresentation.queue(owner, attackedInfo, showInjury))
		owner.addToAnimationStage(usedEvent, std::move(showInjury));
}
