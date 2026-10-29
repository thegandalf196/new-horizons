/*
 * BattleOrderPresentation.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleInterface.h"
#include "BattleAnimationClasses.h"
#include "BattleEffectsController.h"
#include "BattleHero.h"
#include "BattleStacksController.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsHeroLuck.h"
#include "../../lib/battle/NewHorizonsFrozen.h"

namespace
{
// Own the existing nonmagical raise-arm gesture through the ordinary battle
// barrier, including Orders with no visible troop recipients.
class HeroOrderGestureAnimation final : public BattleAnimation
{
	std::shared_ptr<BattleHero> hero;
	bool finished = false;
	std::function<void()> onFinished;

	bool init() override
	{
		hero->setPhase(EHeroAnimType::VICTORY);
		hero->play();
		hero->onPhaseFinished([this]() { finished = true; });
		return true;
	}

public:
	HeroOrderGestureAnimation(BattleInterface & owner, std::shared_ptr<BattleHero> hero,
		std::function<void()> onFinished)
		: BattleAnimation(owner), hero(std::move(hero)), onFinished(std::move(onFinished))
	{
	}

	~HeroOrderGestureAnimation() override
	{
		hero->onPhaseFinished({});
	}

	void tick(uint32_t) override
	{
		if(finished)
		{
			// Resetting the owner's unique_ptr destroys this animation. Keep the
			// callable alive independently until its invocation has returned.
			const auto complete = onFinished;
			complete();
		}
	}
};
}

void BattleInterface::presentAcceptedHeroOrder(const BattleAction & action)
{
	// Consume before playback: both-side/hotseat callbacks and duplicate
	// EndAction delivery must not replay the same presentation.
	const auto pending = std::move(pendingHeroOrderPresentation);
	if(!pending || action.actionType != EActionType::HERO_COMMAND || action.metamagicDecline
		|| pending->side != action.side || pending->command != action.command
		|| pendingHeroOrderRound != getBattle()->battleGetRound()
		|| !getBattle()->getBattle()->getHeroCommandUsed(action.side)
		|| (getBattle()->getBattle()->getActiveOrder(action.side) != action.command
			&& !getBattle()->battleGetHeroOrderState(action.side, action.command)))
		return;

	// Observe only the public ledger transition. Requalifying the grant with
	// a player-scoped callback could hide an enemy hero's accepted entitlement.
	if(!pendingHeroOrderGrantIds.empty())
	{
		const auto & ledger = getBattle()->getBattle()->getHeroActionAllowances(action.side);
		const auto wasConsumed = [&ledger](uint32_t id)
		{
			return std::ranges::none_of(ledger.grants, [id](const auto & grant) { return grant.id == id; });
		};
		if(std::ranges::none_of(pendingHeroOrderGrantIds, wasConsumed))
			return;
	}

	const auto state = getBattle()->battleGetHeroOrderState(action.side, action.command);
	if(heroCommands::isCanonicalRules(getBattle()->getBattle()->getHeroCommandRules())
		&& (!state || state->command != action.command || state->issuedRound != pendingHeroOrderRound))
		return;

	// The pending action was consumed above, so repeated EndAction cannot
	// replay either proc. Successful Luck and Morale have separate receipts.
	if(state)
		presentHeroPositiveProcs(action.side,
			state->heroLuck && state->heroLuck->has(newHorizonsHeroLuck::Proc::LuckyDay), false, false,
			state->heroMorale && state->heroMorale->has(newHorizonsHeroLuck::Proc::HeroicSpirit),
			false, static_cast<int>(action.command),
			state->heroLuck ? &*state->heroLuck : nullptr,
			state->heroMorale ? &*state->heroMorale : nullptr, state->heroMoraleActionGrantId);

	// Reuse presentation resources only, never spell definitions/mechanics.
	// Each Order has a distinct troop effect; Second Wind uses retired Mirth.
	const char * animation = nullptr;
	const char * sound = nullptr;
	float transparency = 1.f;
	switch(action.command)
	{
		case HeroCommand::CHARGE: animation = "C15SPA0"; sound = "TAILWIND"; break; // Haste
		case HeroCommand::HOLD_THE_LINE: animation = "C16SPE"; sound = "TUFFSKIN"; break; // Stone Skin
		case HeroCommand::RIPOSTE: animation = "C04SPA0"; sound = "CNTRSTRK"; break;
		case HeroCommand::BRACE: animation = "C13SPE0"; sound = "SHIELD"; break;
		case HeroCommand::PROTECT: animation = "C01SPA0"; sound = "AIRSHELD"; break;
		case HeroCommand::FOCUS_FIRE: animation = "C12SPA0"; sound = "PRECISON"; break;
		case HeroCommand::FLANK: animation = "C07SPA1"; sound = "DISRUPTR"; transparency = 0.5f; break;
		case HeroCommand::FEINT: break; // Native creature defence, without a spell overlay.
		case HeroCommand::VANGUARD: animation = "C01SPW"; sound = "BLESS"; break; // Rally cue, resources only.
		case HeroCommand::SECOND_WIND: animation = "C09SPW0"; sound = "MIRTH"; break;
		default: return;
	}

	BattleHexArray targets;
	const auto addTarget = [this, &targets](uint32_t unitId)
	{
		const auto * unit = getBattle()->battleGetUnitByID(unitId);
		if(unit && unit->alive() && !unit->isGhost() && unit->getPosition().isValid())
			targets.insert(unit->getPosition());
	};
	if(action.command == HeroCommand::VANGUARD && state && state->revision)
	{
		for(const auto * unit : getBattle()->battleAliveUnits())
			if(state->revision->contains(unit->unitId()) && !unit->isTimeStopped())
				addTarget(unit->unitId());
	}
	else if(action.command == HeroCommand::FEINT)
	{
		for(const auto * stack : getBattle()->battleGetAllStacks(true))
			if(stack && stack->alive() && !stack->isGhost()
				&& getBattle()->battleGetOwner(stack) == getBattle()->sideToPlayer(action.side)
				&& !stack->isTurret() && !stack->hasBonusOfType(BonusType::SIEGE_WEAPON)
				&& stack->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
				&& !stack->isTimeStopped() && !stack->isFrozen() && !newHorizonsFrozen::isFrozen(*stack))
				stacksController->addNewAnim(new DefenceAnimation(*this, stack));
	}
	else if(state && state->primaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID)
	{
		addTarget(state->primaryTargetUnitId);
		if(state->secondaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID)
			addTarget(state->secondaryTargetUnitId);
	}
	else if(!action.target.empty())
	{
		for(const auto & target : action.target)
			if(target.unitValue >= 0)
				addTarget(static_cast<uint32_t>(target.unitValue));
	}
	else
	{
		for(const auto * unit : getBattle()->battleAliveUnits())
			if(getBattle()->battleGetOwner(unit) == getBattle()->sideToPlayer(action.side)
				&& !unit->isGhost() && !unit->isTurret()
				&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
				&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER)
				addTarget(unit->unitId());
	}

	const auto hero = action.side == BattleSide::DEFENDER ? defendingHero : attackingHero;
	if(hero)
	{
		heroOrderGesture = std::make_unique<HeroOrderGestureAnimation>(*this, hero,
			[this]() { heroOrderGesture.reset(); });
		stacksController->addNewAnim(heroOrderGesture.get());
	}
	if(animation && !targets.empty())
		effectsController->displayAnimation(AnimationPath::builtin(animation), AudioPath::builtin(sound), targets, transparency);
}
