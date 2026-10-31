/*
 * StackWithBonuses.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "HypotheticEffectSnapshotRules.h"
#include "../../lib/bonuses/RecipientBonusOverlay.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/battle/UnitQueryTerrain.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/battle/NewHorizonsEnchantedCommand.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/entities/creature/NewHorizonsRecruitmentTraining.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/CStack.h"
#include <typeinfo>


using hypotheticEffectSnapshotRules::hasElementalMemory;
using hypotheticEffectSnapshotRules::hasSnapshotHeroSpecialEffects;
using hypotheticEffectSnapshotRules::hasTimedMagicalSpellEffects;
using hypotheticEffectSnapshotRules::isCapturedProjectionEffect;
using hypotheticEffectSnapshotRules::isInPhysicalAfflictionGroup;

namespace hypotheticEffectSnapshotRules
{
bool hasElementalMemory(const battle::Unit & unit)
{
	return unit.hasBonus(CSelector([](const Bonus * bonus)
	{
		return bonus && newHorizonsElementalRebirth::isElementalMemoryBonus(*bonus);
	}));
}

bool timedProjectionEffect(const Bonus * bonus,
	const std::set<std::pair<BonusSource, BonusSourceID>> & groupHistory)
{
	return Bonus::NTurns(bonus) && isCapturedProjectionEffect(bonus, groupHistory);
}

bool hasTimedMagicalSpellEffects(const IBonusBearer & bearer)
{
	return newHorizonsSorcery::isSpellLocked(bearer) || bearer.hasBonus(CSelector([](const Bonus * bonus)
	{
		return bonus && bonus->turnsRemain > 0
			&& (newHorizonsSorcery::isPreservedSpellLockEffect(bonus, true)
				|| newHorizonsSorcery::isPreservedSpellLockEffect(bonus, false));
	}));
}

bool hasSnapshotHeroSpecialEffects(const IBonusBearer & bearer)
{
	// Snapshot identified arrival timers and Inspirational activation receipts
	// before a lazy descendant can observe a later parent/live boundary.
	return bearer.hasBonus(CSelector(isSnapshotHeroSpecialEffect));
}

}

void StackWithBonuses::setOriginalBearer(const IBonusBearer * bearer)
{
	origBearer = bearer;
	const auto * projected = dynamic_cast<const StackWithBonuses *>(bearer);
	if(projected)
	{
		capturedPhysicalAfflictionGroups = projected->capturedPhysicalAfflictionGroups;

		projectedBearer = projected->weak_from_this().lock();
		ownedBearer = projected->ownedBearer;
		if(projectedBearer)
			origBearer = projectedBearer.get();
	}
	originalUnit = dynamic_cast<const battle::Unit *>(origBearer);
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::CUnitState * Stack)
	: battle::CUnitState(),
	origBearer(nullptr),
	owner(Owner),
	type(Stack->unitType()),
	sourceCreatureType(Stack->unitType()->getId()),
	baseAmount(Stack->unitBaseAmount()),
	battleStartMaximumAggregateHP(Stack->getBattleStartMaximumAggregateHP()),
	rebirthOriginalAggregateHP(Stack->getRebirthOriginalAggregateHP()),
	id(Stack->unitId()),
	side(Stack->unitSide()),
	player(Stack->unitOwner()),
	slot(Stack->unitSlot()),
	treeVersionLocal(0)
{
	setOriginalBearer(Stack->getBonusBearer());
	localInit(Owner);

	battle::CUnitState::operator=(*Stack);
	const bool snapshotSpellTimers = hasTimedMagicalSpellEffects(*this);
	if(snapshotSpellTimers)
		captureLocalSpellEffects();
	if(newHorizonsFrozen::isFrozen(*this) || newHorizonsSwiftRebirth::lifecycle(*this)
		|| hasElementalMemory(*this)
		|| hasBonus(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus))
		|| hasBonus(CSelector(newHorizonsTraining::isTrainingBonus)) || snapshotSpellTimers
		|| hasSnapshotHeroSpecialEffects(*this))
		captureEffects();
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::Unit * Stack)
	: battle::CUnitState(),
	origBearer(nullptr),
	owner(Owner),
	type(Stack->unitType()),
	sourceCreatureType(Stack->unitType()->getId()),
	baseAmount(Stack->unitBaseAmount()),
	battleStartMaximumAggregateHP(Stack->getBattleStartMaximumAggregateHP()),
	rebirthOriginalAggregateHP(Stack->getRebirthOriginalAggregateHP()),
	id(Stack->unitId()),
	side(Stack->unitSide()),
	player(Stack->unitOwner()),
	slot(Stack->unitSlot()),
	treeVersionLocal(0)
{
	setOriginalBearer(Stack->getBonusBearer());
	localInit(Owner);

	auto state = Stack->acquireState();
	battle::CUnitState::operator=(*state);
	const bool snapshotSpellTimers = hasTimedMagicalSpellEffects(*this);
	if(snapshotSpellTimers)
		captureLocalSpellEffects();
	if(newHorizonsFrozen::isFrozen(*this) || newHorizonsSwiftRebirth::lifecycle(*this)
		|| hasElementalMemory(*this)
		|| hasBonus(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus))
		|| hasBonus(CSelector(newHorizonsTraining::isTrainingBonus)) || snapshotSpellTimers
		|| hasSnapshotHeroSpecialEffects(*this))
		captureEffects();
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::UnitInfo & info)
	: battle::CUnitState(),
	origBearer(nullptr),
	owner(Owner),
	type(info.type.toCreature()),
	sourceCreatureType(info.type),
	baseAmount(info.count),
	rebirthOriginalAggregateHP(info.rebirthOriginalAggregateHP),
	id(info.id),
	side(info.side),
	slot(SlotID::SUMMONED_SLOT_PLACEHOLDER),
	treeVersionLocal(0)
{
	player = Owner->getSidePlayer(side);
	CStackBasicDescriptor descriptor(info.type, info.count);
	// Capture terrain only from this exact bound source, not battle/updater membership.
	const auto * army = Owner->getSideArmy(side);
	const auto terrain = captureUnitQueryTerrain(army);
	ownedBearer = std::make_shared<CStack>(&descriptor, player, static_cast<int>(id), side,
		SlotID::SUMMONED_SLOT_PLACEHOLDER, true, terrain);
	ownedBearer->summoned = info.summoned;
	ownedBearer->natureSummoned = info.natureSummoned;
	ownedBearer->summonRoundsRemaining = info.summonRoundsRemaining;
	ownedBearer->initialPosition = info.position;
	setOriginalBearer(ownedBearer.get());
	if(army)
		ownedBearer->attachToSource(*army);
	ownedBearer->attachToSource(*type);

	localInit(Owner);

	position = info.position;
	summoned = info.summoned;
	natureSummoned = info.natureSummoned;
	summonRoundsRemaining = info.summonRoundsRemaining;
	if(info.phantomIntegrity > 0)
		initializePhantomProfile(info.phantomIntegrity, info.phantomDuration);
}

StackWithBonuses::~StackWithBonuses() = default;

StackWithBonuses & StackWithBonuses::operator=(const battle::CUnitState & other)
{
	battle::CUnitState::operator=(other);
	battleStartMaximumAggregateHP = other.getBattleStartMaximumAggregateHP();
	rebirthOriginalAggregateHP = other.getRebirthOriginalAggregateHP();
	return *this;
}

const CCreature * StackWithBonuses::unitType() const
{
	return hasBattleFormState() ? battleFormCreature().toCreature() : type;
}

int32_t StackWithBonuses::unitBaseAmount() const
{
	return baseAmount;
}

uint32_t StackWithBonuses::unitId() const
{
	return id;
}

BattleSide StackWithBonuses::unitSide() const
{
	return side;
}

PlayerColor StackWithBonuses::unitOwner() const
{
	return player;
}

SlotID StackWithBonuses::unitSlot() const
{
	return slot;
}

int64_t StackWithBonuses::getBattleStartMaximumAggregateHP() const
{
	return battleStartMaximumAggregateHP;
}

int64_t StackWithBonuses::getRebirthOriginalAggregateHP() const
{
	return rebirthOriginalAggregateHP;
}

TConstBonusListPtr StackWithBonuses::getAllBonuses(const CSelector & selector, const std::string & cachingStr) const
{
	const int32_t level = newHorizonsCreatureAbilitySuppression::suppressionLevel(*this);
	if(level == 0)
		return getBonusesBeforeCreatureAbilitySuppression(selector, cachingStr, false);

	// Suppress intrinsic bonuses before stacking. Otherwise an intrinsic ability
	// could already have been combined with another source of the same type and
	// become impossible to remove without also losing that unrelated source.
	const auto baseline = getBonusesBeforeCreatureAbilitySuppression(selector, cachingStr, true);
	return newHorizonsCreatureAbilitySuppression::filterBonuses(*this, baseline, level, true);
}

TConstBonusListPtr StackWithBonuses::getUnstackedBonuses(const CSelector & selector) const
{
	const int32_t level = newHorizonsCreatureAbilitySuppression::suppressionLevel(*this);
	const auto baseline = getBonusesBeforeCreatureAbilitySuppression(selector, {}, true);
	return level == 0 ? baseline
		: newHorizonsCreatureAbilitySuppression::filterBonuses(*this, baseline, level, false);
}

TConstBonusListPtr StackWithBonuses::getBonusesBeforeCreatureAbilitySuppression(
	const CSelector & selector, const std::string & cachingStr, const bool unstacked) const
{
	if(!damageScoringBonuses)
		if(const auto subject = owner->initialExportSubject())
			return newHorizonsDiscipline::veteranCohesionBonuses(
				subject->query(*owner, id, selector, unstacked), selector, veteranCohesionEarned);
	return mergeBonuses(selector, cachingStr, unstacked);
}

bool StackWithBonuses::hasStableInheritedBonusList() const
{
	// Native request caches replace list owners, never a held query container.
	// Foreign/derived implementations and typed export queries are not inferred.
	if(!origBearer)
		return false;
	if(typeid(*origBearer) == typeid(CStack))
		return true;
	if(typeid(*origBearer) != typeid(StackWithBonuses))
		return false;
	const auto * projected = dynamic_cast<const StackWithBonuses *>(origBearer);
	return !projected->owner->initialExportSubject();
}


int32_t StackWithBonuses::getTreeVersion() const
{
	auto result = owner->getTreeVersion();
	return result + treeVersionLocal + getBattleFormViewRevision() + (veteranCohesionEarned ? 1 : 0);
}

void StackWithBonuses::onBattleFormChanged()
{
	battle::CUnitState::onBattleFormChanged();
}

std::string StackWithBonuses::getDescription() const
{
	std::ostringstream oss;
	oss << unitOwner().toString();
	oss << " battle stack [" << unitId() << "]: " << getCount() << " of ";
	if(type)
		oss << type->getJsonKey();
	else
		oss << "[UNDEFINED TYPE]";

	oss << " from slot " << slot;

	return oss.str();
}

void StackWithBonuses::spendMana(ServerCallback * server, const int spellCost) const
{
	//TODO: evaluate cast use
}
