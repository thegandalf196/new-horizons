/*
 * CStack.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CStack.h"

#include <vstd/RNG.h>

#include <vcmi/Entity.h>
#include <vcmi/ServerCallback.h>

#include "texts/CGeneralTextHandler.h"
#include "battle/BattleInfo.h"
#include "battle/NewHorizonsCreatureAbilitySuppression.h"
#include "battle/NewHorizonsOffense.h"
#include "battle/NewHorizonsElementalRebirth.h"
#include "spells/NewHorizonsMagic.h"
#include "GameLibrary.h"
#include "networkPacks/PacksForClientBattle.h"
#include "spells/CSpell.h"

///CStack
CStack::CStack(const CStackInstance * Base, const PlayerColor & O, int I, BattleSide Side, const SlotID & S):
	CBonusSystemNode(BonusNodeType::STACK_BATTLE),
	base(Base),
	ID(I),
	typeID(Base->getId()),
	baseAmount(Base->getCount()),
	owner(O),
	slot(S),
	side(Side)
{
	health.init(); //???
	doubleWideCached = battle::CUnitState::doubleWide();
}

CStack::CStack():
	CBonusSystemNode(BonusNodeType::STACK_BATTLE),
	owner(PlayerColor::NEUTRAL),
	slot(SlotID(255)),
	initialPosition(BattleHex())
{
}

CStack::CStack(const CStackBasicDescriptor * stack, const PlayerColor & O, int I, BattleSide Side,
	const SlotID & S, const bool hypothetical):
	CBonusSystemNode(BonusNodeType::STACK_BATTLE, hypothetical),
	ID(I),
	typeID(stack->getId()),
	baseAmount(stack->getCount()),
	owner(O),
	slot(S),
	side(Side)
{
	health.init(); //???
	doubleWideCached = battle::CUnitState::doubleWide();
}

void CStack::localInit(BattleInfo * battleInfo)
{
	battle = battleInfo;
	assert(typeID.hasValue());
	const int32_t restoredPersonalBloodrageIncrement = personalBloodrageIncrement;
	const auto restoredConfusionState = confusionState;

	exportBonuses();
	if(base) //stack originating from "real" stack in garrison -> attach to it
	{
		attachTo(const_cast<CStackInstance&>(*base));
	}
	else //attach directly to obj to which stack belongs and creature type
	{
		CArmedInstance * army = battle->battleGetArmyObject(side);
		assert(army);
		attachToSource(*typeID.toCreature());
		// Attach native sources before entering the battle graph so propagated
		// auras use this owned stack, not the ownerless creature definition.
		attachTo(*army);
	}
	CUnitState::localInit(this); //it causes execution of the CStack::isOnNativeTerrain where nativeTerrain will be considered
	// CUnitState::localInit resets ordinary per-battle transient state; preserve
	// the one personal increment explicitly carried by this binary stack snapshot.
	personalBloodrageIncrement = restoredPersonalBloodrageIncrement;
	// Binary stack descriptors carry pending Confusion and target history too;
	// rebinding the stack to the battle must not consume either value.
	confusionState = restoredConfusionState;
	position = initialPosition;
}

void CStack::afterNewRound(bool isFirstRound)
{
	battle::CUnitState::afterNewRound(isFirstRound);
	const auto guardianSpiritBonuses = getBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT));
	if(!guardianSpiritBonuses || guardianSpiritBonuses->empty())
	{
		guardianSpiritHitPoints = 0;
		guardianSpiritRoundsRemaining = 0;
	}
	else
	{
		int64_t markerHitPoints = 0;
		int32_t roundsRemaining = 0;
		for(const auto & bonus : *guardianSpiritBonuses)
		{
			if(!bonus)
				continue;
			markerHitPoints = std::max<int64_t>(markerHitPoints, bonus->val);
			roundsRemaining = std::max<int32_t>(roundsRemaining, bonus->turnsRemain);
		}
		// A new or loaded marker initializes a missing pool; otherwise preserve
		// its partially consumed saved value and only mirror timed expiry.
		if(guardianSpiritHitPoints == 0)
			guardianSpiritHitPoints = markerHitPoints;
		guardianSpiritRoundsRemaining = roundsRemaining;
	}
	removeBonusesRecursive(CSelector([](const Bonus * bonus)
	{
		return newHorizonsOffense::isNoQuarterRetaliationBonus(bonus);
	}));
}

bool CStack::acceptsBonus(const Bonus & bonus) const
{
	if(hasBattleForm() && battleFormCreature() != battleFormOriginalCreature()
		&& bonus.sid == BonusSourceID(battleFormOriginalCreature())
		&& (bonus.source == BonusSource::CREATURE_ABILITY || bonus.source == BonusSource::STACK_EXPERIENCE))
		return false;

	// Temporary summons do not inherit Sylvan Luck rank effects unless they
	// carry Nature-spell provenance and their hero owns Wild Chance.  Filtering
	// by source skill preserves native creature Luck and unrelated bonuses.
	if(!summoned || bonus.source != BonusSource::SECONDARY_SKILL)
		return true;

	// Identifier resolution depends on VLC and therefore must never run during
	// namespace-scope initialization (notably in the standalone test binary).
	const SecondarySkill sylvanLuckSkill(SecondarySkill::decode("new-horizons:sylvanLuck"));
	if(bonus.sid.as<SecondarySkill>() != sylvanLuckSkill)
		return true;

	const auto * hero = getMyHero();
	return natureSummoned && hero
		&& hero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.wildChance");
}

TConstBonusListPtr CStack::getBonusesBeforeCreatureAbilitySuppression(const CSelector & selector,
	const std::string & cachingStr, const bool unstacked) const
{
	return unstacked
		? CBonusSystemNode::getUnstackedBonuses(selector)
		: CBonusSystemNode::getAllBonuses(selector, cachingStr);
}

TConstBonusListPtr CStack::getAllBonuses(const CSelector & selector, const std::string & cachingStr) const
{
	const int32_t level = newHorizonsCreatureAbilitySuppression::suppressionLevel(*this);
	if(level == 0)
		return CBonusSystemNode::getAllBonuses(selector, cachingStr);

	const auto baseline = getBonusesBeforeCreatureAbilitySuppression(selector, cachingStr, true);
	return newHorizonsCreatureAbilitySuppression::filterBonuses(*this, baseline, level, true);
}

TConstBonusListPtr CStack::getUnstackedBonuses(const CSelector & selector) const
{
	const int32_t level = newHorizonsCreatureAbilitySuppression::suppressionLevel(*this);
	if(level == 0)
		return CBonusSystemNode::getUnstackedBonuses(selector);

	const auto baseline = getBonusesBeforeCreatureAbilitySuppression(selector, {}, true);
	return newHorizonsCreatureAbilitySuppression::filterBonuses(*this, baseline, level, false);
}

int32_t CStack::unitLevel() const
{
	if(base)
		return base->getLevel(); //creature or commander
	else
		return std::max(1, static_cast<int>(unitType()->getLevel())); //war machine, clone etc
}

si32 CStack::magicResistance() const
{
	return CUnitState::magicResistance();
}

std::vector<SpellID> CStack::activeSpells() const
{
	std::vector<SpellID> ret;

	std::stringstream cachingStr;
	cachingStr << "!type_" << vstd::to_underlying(BonusType::NONE) << "source_" << vstd::to_underlying(BonusSource::SPELL_EFFECT);
	CSelector selector = Selector::sourceType()(BonusSource::SPELL_EFFECT)
						 .And(CSelector([](const Bonus * b)->bool
	{
		return b->type != BonusType::NONE && b->sid.as<SpellID>().toSpell() && !b->sid.as<SpellID>().toSpell()->isAdventure();
	}));

	TConstBonusListPtr spellEffects = getBonuses(selector, cachingStr.str());
	for(const auto & it : *spellEffects)
	{
		if(!vstd::contains(ret, it->sid.as<SpellID>()))  //do not duplicate spells with multiple effects
			ret.push_back(it->sid.as<SpellID>());
	}

	return ret;
}

CStack::~CStack()
{
	detachFromAll();
}

const CGHeroInstance * CStack::getMyHero() const
{
	if(base)
		return dynamic_cast<const CGHeroInstance *>(base->getArmy());
	else //we are attached directly?
		for(const CBonusSystemNode * n : getParentNodes())
			if(n->getNodeType() == BonusNodeType::HERO)
				return dynamic_cast<const CGHeroInstance *>(n);

	return nullptr;
}

std::string CStack::nodeName() const
{
	std::ostringstream oss;
	oss << owner.toString();
	oss << " battle stack [" << ID << "]: " << getCount() << " of ";
	if(typeID.hasValue())
		oss << typeID.toEntity(LIBRARY)->getJsonKey();
	else
		oss << "[UNDEFINED TYPE]";

	oss << " from slot " << slot;
	if(base && base->getArmy())
		oss << " of armyobj=" << base->getArmy()->id.getNum();
	return oss.str();
}

void CStack::prepareAttacked(BattleStackAttacked & bsa, vstd::RNG & rand, bool destroyRemains,
	battle::DamageProvenance provenance) const
{
	auto newState = acquireState();
	prepareAttacked(bsa, rand, newState, destroyRemains, false, provenance);
}

void CStack::prepareAttacked(BattleStackAttacked & bsa, vstd::RNG & rand,
	const std::shared_ptr<battle::CUnitState> & customState, bool destroyRemains,
	const bool bypassTemporaryHitPoints, battle::DamageProvenance provenance)
{
	auto initialCount = customState->getCount();
	const auto guardianSpiritBefore = customState->guardianSpiritHitPoints;

	// compute damage and update bsa.damageAmount
	if(bypassTemporaryHitPoints)
		customState->damageShadowGiftSacrifice(bsa.damageAmount);
	else
		customState->damage(bsa.damageAmount, destroyRemains, provenance);
	if(guardianSpiritBefore > 0 && customState->guardianSpiritHitPoints == 0)
		bsa.flags |= BattleStackAttacked::GUARDIAN_SPIRIT_EXHAUSTED;

	bsa.killedAmount = initialCount - customState->getCount();

	if(!customState->alive() && customState->isClone())
	{
		bsa.flags |= BattleStackAttacked::CLONE_KILLED;
	}
	else if(!customState->alive()) //stack killed
	{
		bsa.flags |= BattleStackAttacked::KILLED;

		auto resurrectValue = customState->valOfBonuses(BonusType::REBIRTH);

		if(resurrectValue > 0 && customState->canCast()
			&& customState->getPhantomInitialIntegrity() == 0) //phantoms cannot be rebirthed
		{
			double resurrectFactor = resurrectValue / 100.0;

			auto baseAmount = customState->unitBaseAmount();

			double resurrectedRaw = baseAmount * resurrectFactor;

			auto resurrectedCount = static_cast<int32_t>(floor(resurrectedRaw));

			auto resurrectedAdd = static_cast<int32_t>(baseAmount - (resurrectedCount / resurrectFactor));

			for(int32_t i = 0; i < resurrectedAdd; i++)
			{
				if(resurrectValue > rand.nextInt(0, 99))
					resurrectedCount += 1;
			}

			if(customState->hasBonusOfType(BonusType::REBIRTH, BonusCustomSubtype::rebirthSpecial))
			{
				// resurrect at least one Sacred Phoenix
				vstd::amax(resurrectedCount, 1);
			}

			if(resurrectedCount > 0)
			{
				int64_t toHeal = customState->getMaxHealth() * resurrectedCount;
				//TODO: add one-battle rebirth?
				auto rebirth = customState->heal(toHeal, EHealLevel::RESURRECT, EHealPower::PERMANENT);
				if(rebirth.resurrectedCount > 0)
				{
					customState->casts.use();
					bsa.flags |= BattleStackAttacked::REBIRTH;
					customState->counterAttacks.use(customState->counterAttacks.available());
				}
			}
		}
	}

	// New Horizons' disintegration-style damage gets one chance to trigger
	// rebirth above, then permanently removes a stack that still has no living
	// remains.  This mirrors the legacy ghost path without changing ordinary
	// weapon damage or the legacy DISINTEGRATE bonus.
	// Remove the corpse only when every creature in the original stack has
	// unusable remains. A lethal Disintegrate hit after ordinary casualties must
	// leave those earlier bodies available to resurrection and Necromancy.
	if(destroyRemains && !customState->alive()
		&& customState->getUnusableRemains() >= customState->unitBaseAmount())
		customState->ghostPending = true;

	bsa.newState.data = customState->save();
	bsa.newState.healthDelta = -bsa.damageAmount;
	bsa.newState.id = customState->unitId();
	bsa.newState.operation = UnitChanges::EOperation::UPDATE;
}

std::string CStack::getName() const
{
	const auto * type = unitType();
	return (getCount() == 1) ? type->getNameSingularTranslated() : type->getNamePluralTranslated(); //War machines can't use base
}

bool CStack::canBeHealed() const
{
	return getFirstHPleft() < static_cast<int32_t>(getMaxHealth()) && isValidTarget() && !hasBonusOfType(BonusType::SIEGE_WEAPON);
}

bool CStack::isOnNativeTerrain() const
{
	return isNativeTerrain(getCurrentTerrain());
}

TerrainId CStack::getCurrentTerrain() const
{
	return battle->getTerrainType();
}

const CCreature * CStack::unitType() const
{
	if(hasBattleForm())
		return battleFormCreature().toCreature();
	return typeID.toCreature();
}

void CStack::onBattleFormChanged()
{
	battle::CUnitState::onBattleFormChanged();

	const CCreature * desiredBonusSource = nullptr;
	if(hasBattleForm() && battleFormCreature() != typeID)
		desiredBonusSource = battleFormCreature().toCreature();

	if(formBonusSource != desiredBonusSource)
	{
		if(formBonusSource)
			detachFromSource(*formBonusSource);
		formBonusSource = desiredBonusSource;
		if(formBonusSource)
			attachToSource(*formBonusSource);
	}

	doubleWideCached = unitType()->isDoubleWide();
	nodeHasChanged();
}

bool isBattleFormNativeBonus(const Bonus * bonus, const CreatureID creature)
{
	return bonus
		&& (bonus->source == BonusSource::CREATURE_ABILITY || bonus->source == BonusSource::STACK_EXPERIENCE)
		&& bonus->sid == BonusSourceID(creature);
}

TConstBonusListPtr getBattleFormNativeBonuses(
	const battle::CUnitState & formState,
	const CStack * sourceStack,
	const CArmedInstance * fallbackArmy,
	const CSelector & selector,
	bool unstacked)
{
	const CreatureID originalCreature = formState.battleFormOriginalCreature();
	const CreatureID effectiveCreature = formState.battleFormCreature();
	if(!originalCreature.hasValue() || !effectiveCreature.hasValue())
		return std::make_shared<BonusList>();

	CStackBasicDescriptor descriptor(originalCreature, formState.unitBaseAmount());
	CStack evaluator(&descriptor, formState.unitOwner(), static_cast<int>(formState.unitId()),
		formState.unitSide(), formState.unitSlot(), true);
	if(sourceStack)
		evaluator.base = sourceStack->base;

	if(sourceStack && sourceStack->getBattle())
	{
		evaluator.localInit(const_cast<BattleInfo *>(sourceStack->getBattle()));
	}
	else if(sourceStack && sourceStack->base)
	{
		evaluator.attachTo(const_cast<CStackInstance &>(*sourceStack->base));
	}
	else if(sourceStack)
	{
		bool originalSourceAttached = false;
		bool armySourceAttached = false;
		for(const auto * parent : sourceStack->getParentNodes())
		{
			evaluator.attachToSource(*parent);
			originalSourceAttached = originalSourceAttached || parent == originalCreature.toCreature();
			armySourceAttached = armySourceAttached || parent == fallbackArmy;
		}
		if(fallbackArmy && !armySourceAttached)
			evaluator.attachToSource(*fallbackArmy);
		if(!originalSourceAttached)
			evaluator.attachToSource(*originalCreature.toCreature());
	}
	else
	{
		if(fallbackArmy)
			evaluator.attachToSource(*fallbackArmy);
		evaluator.attachToSource(*originalCreature.toCreature());
	}

	// Establish the requested form only after the evaluator has its real context.
	// It remains hypothetical throughout, so source attachment is read-only.
	static_cast<battle::CUnitState &>(evaluator) = formState;
	const CSelector nativeSelector([effectiveCreature, &selector](const Bonus * bonus)
	{
		return isBattleFormNativeBonus(bonus, effectiveCreature) && selector(bonus);
	});
	return unstacked ? evaluator.getUnstackedBonuses(nativeSelector)
		: evaluator.getAllBonuses(nativeSelector);
}

int32_t CStack::unitBaseAmount() const
{
	return baseAmount;
}

void CStack::initializeRebirthOriginalAggregateHP(const int64_t originalHP)
{
	if(originalHP < 0)
		throw std::invalid_argument("Rebirth output original HP cannot be negative");
	if(rebirthOriginalAggregateHP != 0 && rebirthOriginalAggregateHP != originalHP)
		throw std::logic_error("Rebirth output original HP is immutable");
	rebirthOriginalAggregateHP = originalHP;
}

void CStack::captureBattleStartMaximumAggregateHP()
{
	const auto * combatHero = battle ? battle->battleGetFightingHero(unitSide()) : getMyHero();
	if(battleStartMaximumAggregateHP > 0
		|| !newHorizonsElementalRebirth::isEligibleSource(*this)
		|| !newHorizonsElementalRebirth::activeProfile(combatHero))
		return;

	const auto maximumPerCreature = getMaxHealth();
	if(maximumPerCreature == 0 || unitBaseAmount() <= 0)
		return;

	const auto aggregate = static_cast<int64_t>(unitBaseAmount()) * maximumPerCreature;
	if(aggregate > 0)
		battleStartMaximumAggregateHP = aggregate;
}

const IBonusBearer* CStack::getBonusBearer() const
{
	return this;
}

bool CStack::unitHasAmmoCart(const battle::Unit * unit) const
{
	return battle->battleUnitHasAmmoCart(unit);
}

PlayerColor CStack::unitEffectiveOwner(const battle::Unit * unit) const
{
	return battle->battleGetOwner(unit);
}

int CStack::unitFortuneSpeed(const battle::Unit * unit) const
{
	return battle->battleFortuneSpeed(unit);
}

int CStack::unitSpeedBonus(const battle::Unit * unit) const
{
	return battle->battleBloodrageSpeed(unit);
}

int CStack::unitAdditionalRetaliations(const battle::Unit * unit) const
{
	return battle->battleBloodrageRetaliations(unit);
}

int CStack::unitBloodragePainIncrement(const battle::Unit * unit) const
{
	return battle ? battle->battleBloodragePainIncrement(unit) : 0;
}

std::optional<int> CStack::unitMagicResistance(const battle::Unit * unit) const
{
	if(!battle || !unit)
		return std::nullopt;
	return battle->battleGetMagicResistance(unit);
}

std::optional<std::pair<int32_t, int32_t>> CStack::unitMoraleLimits(const battle::Unit * unit) const
{
	if(!battle || !unit)
		return std::nullopt;
	return newHorizonsMagic::moraleLimits(battle->getMagicRules());
}

uint32_t CStack::unitId() const
{
	return ID;
}

BattleSide CStack::unitSide() const
{
	return side;
}

PlayerColor CStack::unitOwner() const
{
	return owner;
}

SlotID CStack::unitSlot() const
{
	return slot;
}

std::string CStack::getDescription() const
{
	return nodeName();
}

void CStack::spendMana(ServerCallback * server, const int spellCost) const
{
	if(spellCost != 1)
		logGlobal->warn("Unexpected spell cost %d for creature", spellCost);

	BattleSetStackProperty ssp;
	ssp.battleID = battle->battleID;
	ssp.stackID = unitId();
	ssp.which = BattleSetStackProperty::CASTS;
	ssp.val = -spellCost;
	ssp.absolute = false;
	server->apply(ssp);
}

void CStack::postDeserialize(const CArmedInstance * army)
{
	if(slot == SlotID::COMMANDER_SLOT_PLACEHOLDER)
	{
		const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
		assert(hero);
		base = hero->getCommander();
	}
	else if(slot == SlotID::SUMMONED_SLOT_PLACEHOLDER || slot == SlotID::ARROW_TOWERS_SLOT || slot == SlotID::WAR_MACHINES_SLOT)
	{
		//no external slot possible, so no base stack
		base = nullptr;
	}
	else if(!army || slot == SlotID() || !army->hasStackAtSlot(slot))
	{
		throw std::runtime_error(typeID.toEntity(LIBRARY)->getNameSingularTranslated() + " doesn't have a base stack!");
	}
	else
	{
		base = &army->getStack(slot);
	}

	doubleWideCached = battle::CUnitState::doubleWide();
}
