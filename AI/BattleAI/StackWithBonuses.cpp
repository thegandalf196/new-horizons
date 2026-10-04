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
#include "NewHorizonsHexOfPain.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsBloodrage.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/battle/TimeStopState.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/battle/SiegeInfo.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/scripting/ScriptService.h"

#include <vcmi/events/EventBus.h>

#include <unordered_map>

#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/CStack.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/constants/NumericConstants.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"

namespace
{
bool projectedEffect(const Bonus * bonus)
{
	return bonus && (bonus->source == BonusSource::SPELL_EFFECT || bonus->source == BonusSource::HERO_COMMAND);
}

bool isInPhysicalAfflictionGroup(const Bonus * bonus,
	const std::set<std::pair<BonusSource, BonusSourceID>> & groupHistory)
{
	return bonus && groupHistory.contains({bonus->source, bonus->sid});
}

bool isCapturedProjectionEffect(const Bonus * bonus,
	const std::set<std::pair<BonusSource, BonusSourceID>> & groupHistory)
{
	return projectedEffect(bonus) || isInPhysicalAfflictionGroup(bonus, groupHistory);
}

bool timedProjectionEffect(const Bonus * bonus,
	const std::set<std::pair<BonusSource, BonusSourceID>> & groupHistory)
{
	return Bonus::NTurns(bonus) && isCapturedProjectionEffect(bonus, groupHistory);
}

bool isGuardianSpiritBonus(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::GUARDIAN_SPIRIT;
}

std::optional<SpellID> entangleSpellId()
{
	if(!LIBRARY || !LIBRARY->identifiers())
		return std::nullopt;
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell", "new-horizons:entangle", true);
	if(!id || *id < 0)
		return std::nullopt;
	return SpellID(*id);
}

const CSelector & guardianSpiritSelector()
{
	static const CSelector selector([](const Bonus * bonus)
	{
		return isGuardianSpiritBonus(bonus);
	});
	return selector;
}

void applyGuardianSpiritBonus(StackWithBonuses & unit, const Bonus & bonus)
{
	if(!isGuardianSpiritBonus(&bonus))
		return;

	const int32_t rounds = Bonus::NTurns(&bonus) ? std::max<int32_t>(0, bonus.turnsRemain) : 0;
	unit.guardianSpiritHitPoints = rounds > 0 ? std::max<int64_t>(0, bonus.val) : 0;
	unit.guardianSpiritRoundsRemaining = rounds;
}

void applyGuardianSpiritBonuses(StackWithBonuses & unit, const std::vector<Bonus> & bonuses)
{
	for(const auto & bonus : bonuses)
		applyGuardianSpiritBonus(unit, bonus);
}

void replacePhysicalAfflictionMarker(StackWithBonuses & unit, Bonus marker)
{
	physicalAfflictions::markerMetadata(marker);
	const CSelector selector([&marker](const Bonus * bonus)
	{
		return bonus && bonus->type == BonusType::PHYSICAL_AFFLICTION
			&& bonus->source == marker.source && bonus->sid == marker.sid;
	});

	const auto current = unit.getBonuses(selector);
	if(current && !current->empty())
	{
		for(const auto & existing : *current)
			if(existing && existing->duration == marker.duration && Bonus::NTurns(existing.get()))
				marker.turnsRemain = std::max(marker.turnsRemain, existing->turnsRemain);
		unit.removeUnitBonus(selector);
	}
	unit.bonusesToAdd.emplace_back(std::move(marker));
}

void restoreGuardianSpiritFromExistingBonuses(StackWithBonuses & unit)
{
	const auto bonuses = unit.getBonuses(guardianSpiritSelector());
	const Bonus * strongest = nullptr;
	for(const auto & bonus : *bonuses)
		if(bonus && (!strongest || bonus->val > strongest->val))
			strongest = bonus.get();

	if(!strongest)
	{
		unit.guardianSpiritHitPoints = 0;
		unit.guardianSpiritRoundsRemaining = 0;
		return;
	}

	const auto rounds = Bonus::NTurns(strongest)
		? std::max<int32_t>(0, strongest->turnsRemain) : 0;
	unit.guardianSpiritRoundsRemaining = rounds;
	if(rounds == 0)
		unit.guardianSpiritHitPoints = 0;
}

ui8 timeStopSideMask(BattleSide side)
{
	if(side == BattleSide::ATTACKER)
		return 1u;
	if(side == BattleSide::DEFENDER)
		return 2u;
	return 0u;
}

bool isHexOfPainBonus(const Bonus & bonus)
{
	if(bonus.type != BonusType::COMBAT_EVENT_TRIGGER || bonus.source != BonusSource::SPELL_EFFECT
		|| !bonus.parameters || bonus.sid.toString() != newHorizonsHexOfPainAI::SPELL_ID)
		return false;

	if(bonus.subtype.toString() != newHorizonsHexOfPainAI::TRIGGER_ID)
		return false;

	const auto & parameters = bonus.parameters->toCustom<JsonNode>();
	return parameters["damageSharePercent"].isNumber()
		&& !parameters["casterSide"].isNull();
}
}

bool newHorizonsHexOfPainAI::hasEffect(const battle::Unit * unit)
{
	if(!unit)
		return false;

	const auto triggers = unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	return std::ranges::any_of(*triggers, [](const auto & bonus)
	{
		return bonus && isHexOfPainBonus(*bonus);
	});
}

int newHorizonsHexOfPainAI::effectRounds(const battle::Unit * unit)
{
	if(!unit)
		return 0;

	int rounds = 0;
	const auto triggers = unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
		if(bonus && isHexOfPainBonus(*bonus) && Bonus::NTurns(bonus.get()))
			rounds = std::max(rounds, static_cast<int>(bonus->turnsRemain));
	return std::max(0, rounds);
}

void actualizeEffect(TBonusListPtr target, const Bonus & ef)
{
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> updatedCopies;
	for(auto & bonus : *target) //TODO: optimize
	{
		if(bonus->source == ef.source && bonus->sid == ef.sid && bonus->type == ef.type
			&& bonus->subtype == ef.subtype && bonus->valType == ef.valType)
		{
			if(bonus->turnsRemain < ef.turnsRemain)
			{
				auto [copy, inserted] = updatedCopies.try_emplace(bonus.get());
				if(inserted)
				{
					copy->second = std::make_shared<Bonus>(*bonus);
					copy->second->turnsRemain = ef.turnsRemain;
				}
				bonus = copy->second;
			}
		}
	}
}

void StackWithBonuses::setOriginalBearer(const IBonusBearer * bearer)
{
	origBearer = bearer;
	const auto * projected = dynamic_cast<const StackWithBonuses *>(bearer);
	if(!projected)
		return;
	capturedPhysicalAfflictionGroups = projected->capturedPhysicalAfflictionGroups;

	projectedBearer = projected->weak_from_this().lock();
	ownedBearer = projected->ownedBearer;
	if(projectedBearer)
		origBearer = projectedBearer.get();
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::CUnitState * Stack)
	: battle::CUnitState(),
	origBearer(nullptr),
	owner(Owner),
	type(Stack->unitType()),
	sourceCreatureType(Stack->unitType()->getId()),
	baseAmount(Stack->unitBaseAmount()),
	id(Stack->unitId()),
	side(Stack->unitSide()),
	player(Stack->unitOwner()),
	slot(Stack->unitSlot()),
	treeVersionLocal(0)
{
	setOriginalBearer(Stack->getBonusBearer());
	localInit(Owner);

	battle::CUnitState::operator=(*Stack);
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::Unit * Stack)
	: battle::CUnitState(),
	origBearer(nullptr),
	owner(Owner),
	type(Stack->unitType()),
	sourceCreatureType(Stack->unitType()->getId()),
	baseAmount(Stack->unitBaseAmount()),
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
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::UnitInfo & info)
	: battle::CUnitState(),
	origBearer(nullptr),
	owner(Owner),
	type(info.type.toCreature()),
	sourceCreatureType(info.type),
	baseAmount(info.count),
	id(info.id),
	side(info.side),
	slot(SlotID::SUMMONED_SLOT_PLACEHOLDER),
	treeVersionLocal(0)
{
	player = Owner->getSidePlayer(side);
	CStackBasicDescriptor descriptor(info.type, info.count);
	ownedBearer = std::make_shared<CStack>(&descriptor, player, static_cast<int>(id), side,
		SlotID::SUMMONED_SLOT_PLACEHOLDER, true);
	ownedBearer->summoned = info.summoned;
	ownedBearer->natureSummoned = info.natureSummoned;
	ownedBearer->initialPosition = info.position;
	origBearer = ownedBearer.get();
	if(const auto * army = Owner->getSideArmy(side))
		ownedBearer->attachToSource(*army);
	ownedBearer->attachToSource(*type);

	localInit(Owner);

	position = info.position;
	summoned = info.summoned;
	natureSummoned = info.natureSummoned;
	if(info.phantomIntegrity > 0)
		initializePhantomProfile(info.phantomIntegrity, info.phantomDuration);
}

StackWithBonuses::~StackWithBonuses() = default;

StackWithBonuses & StackWithBonuses::operator=(const battle::CUnitState & other)
{
	battle::CUnitState::operator=(other);
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
	return mergeBonuses(selector, cachingStr, unstacked);
}

TConstBonusListPtr StackWithBonuses::mergeBonuses(const CSelector & selector,
	const std::string & cachingStr, bool unstacked) const
{
	auto ret = std::make_shared<BonusList>();
	const CreatureID effectiveCreature = battleFormCreature();
	const bool replaceNativeCreatureBonuses = effectiveCreature != sourceCreatureType;
	// Refresh changes duration, not value. Filtering before merging can hide the
	// existing identity and incorrectly turn a refresh into a new effect.
	const auto & mergeSelector = bonusesToUpdate.empty() ? selector : Selector::all;
	const auto originalCachingStr = bonusesToUpdate.empty() ? cachingStr : std::string();
	TConstBonusListPtr originalList;
	if(const auto * originalUnit = dynamic_cast<const battle::Unit *>(origBearer))
		originalList = originalUnit->getBonusesBeforeCreatureAbilitySuppression(
			mergeSelector, originalCachingStr, unstacked);
	else
		originalList = unstacked
			? origBearer->getUnstackedBonuses(mergeSelector)
			: origBearer->getAllBonuses(mergeSelector, originalCachingStr);
	const bool hasProjectionSnapshot = unstacked
		? projectedUnstackedEffects.has_value() : projectedEffects.has_value();

	vstd::copy_if(*originalList, std::back_inserter(*ret), [this, hasProjectionSnapshot](const std::shared_ptr<Bonus> & b)
	{
		return !vstd::contains(bonusesToRemove, b)
			&& !(hasProjectionSnapshot && isCapturedProjectionEffect(b.get(), capturedPhysicalAfflictionGroups));
	});
	if(replaceNativeCreatureBonuses)
		ret->remove_if(CSelector([this](const Bonus * bonus)
		{
			return isBattleFormNativeBonus(bonus, battleFormOriginalCreature())
				|| isBattleFormNativeBonus(bonus, sourceCreatureType);
		}));

	if(unstacked)
	{
		if(projectedUnstackedEffects)
			for(const auto & bonus : *projectedUnstackedEffects)
				if(bonus && mergeSelector(bonus.get()))
					ret->push_back(bonus);
	}
	else if(projectedEffects)
		for(const auto & bonus : *projectedEffects)
			if(mergeSelector(&bonus))
				ret->push_back(std::make_shared<Bonus>(bonus));

	for(const Bonus & bonus : bonusesToUpdate)
	{
		if(mergeSelector(&bonus))
		{
			const bool markedAfflictionGroup = isInPhysicalAfflictionGroup(&bonus, capturedPhysicalAfflictionGroups);
			const bool refreshableSource = bonus.source == BonusSource::SPELL_EFFECT || markedAfflictionGroup;
			if(refreshableSource && ret->getFirst(Selector::source(bonus.source, bonus.sid)
				.And(Selector::typeSubtypeValueType(bonus.type, bonus.subtype, bonus.valType))))
			{
				actualizeEffect(ret, bonus);
			}
			else
			{
				auto b = std::make_shared<Bonus>(bonus);
				ret->push_back(b);
			}
		}
	}

	for(auto & bonus : bonusesToAdd)
	{
		auto b = std::make_shared<Bonus>(bonus);
		if(mergeSelector(b.get()))
			ret->push_back(b);
	}
	if(replaceNativeCreatureBonuses)
	{
		const CStack * sourceStack = nullptr;
		for(const StackWithBonuses * projected = this; projected && !sourceStack;
			projected = projected->projectedBearer.get())
			sourceStack = dynamic_cast<const CStack *>(projected->origBearer);
		if(!sourceStack && owner)
			sourceStack = dynamic_cast<const CStack *>(owner->battleGetUnitByID(id));
		if(!sourceStack)
			sourceStack = ownedBearer.get();
		const CArmedInstance * fallbackArmy = owner ? owner->getSideArmy(side) : nullptr;
		const auto effectiveBonuses = getBattleFormNativeBonuses(
			*this, sourceStack, fallbackArmy, mergeSelector, unstacked);
		for(const auto & bonus : *effectiveBonuses)
			ret->push_back(std::make_shared<Bonus>(*bonus));
		if(!unstacked)
			ret->stackBonuses();
	}
	if(!bonusesToUpdate.empty())
		ret->remove_if([&](const Bonus * bonus){ return !selector(bonus); });
	//TODO limiters?
	return ret;
}

int32_t StackWithBonuses::getTreeVersion() const
{
	auto result = owner->getTreeVersion();
	return result + treeVersionLocal + getBattleFormViewRevision();
}

void StackWithBonuses::onBattleFormChanged()
{
	battle::CUnitState::onBattleFormChanged();
}

void StackWithBonuses::addUnitBonus(const std::vector<Bonus> & bonus)
{
	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*this, bonus);
	for(const auto & stamped : stampedBonuses)
		if(stamped.type == BonusType::PHYSICAL_AFFLICTION)
			capturedPhysicalAfflictionGroups.emplace(stamped.source, stamped.sid);
	for(const auto & stamped : stampedBonuses)
	{
		if(stamped.type == BonusType::PHYSICAL_AFFLICTION)
			replacePhysicalAfflictionMarker(*this, stamped);
		else
			bonusesToAdd.emplace_back(stamped);
	}
	applyGuardianSpiritBonuses(*this, stampedBonuses);
	treeVersionLocal++;
}

void StackWithBonuses::updateUnitBonus(const std::vector<Bonus> & bonus)
{
	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*this, bonus);
	// Preserve operation order: a preceding local ADD must be visible to refresh.
	captureEffects();
	for(const auto & stamped : stampedBonuses)
		if(stamped.type == BonusType::PHYSICAL_AFFLICTION)
			capturedPhysicalAfflictionGroups.emplace(stamped.source, stamped.sid);
	for(const auto & stamped : stampedBonuses)
	{
		if(stamped.type == BonusType::PHYSICAL_AFFLICTION)
			replacePhysicalAfflictionMarker(*this, stamped);
		else
			bonusesToUpdate.emplace_back(stamped);
	}
	applyGuardianSpiritBonuses(*this, stampedBonuses);
	treeVersionLocal++;
}

void StackWithBonuses::removeUnitBonus(const std::vector<Bonus> & bonus)
{
	for(auto & one : bonus)
	{
		CSelector selector([&one](const Bonus * b) -> bool
		{
			//compare everything but turnsRemain, limiter and propagator
			return one.duration == b->duration
				&& one.type == b->type
				&& one.subtype == b->subtype
				&& one.source == b->source
				&& one.val == b->val
				&& one.sid == b->sid
				&& one.valType == b->valType
				&& one.effectRange == b->effectRange;
		});

		removeUnitBonus(selector);
	}
}

bool StackWithBonuses::applyPurifySelection(const std::vector<SpellID> & spellEffectGroups,
	bool clearPhysicalPoisonState)
{
	bool changed = false;
	bool removePhysicalPoison = clearPhysicalPoisonState;
	for(const auto sourceSpell : spellEffectGroups)
	{
		if(sourceSpell == newHorizonsPurify::physicalPoisonChoiceID())
		{
			removePhysicalPoison = true;
			continue;
		}

		auto group = newHorizonsPurify::spellEffectGroupBonuses(this, sourceSpell);
		if(group.empty())
			continue;

		removeUnitBonus(group);
		changed = true;
	}
	if(removePhysicalPoison)
		changed = newHorizonsPurify::clearPhysicalPoison(this) || changed;
	return changed;
}

void StackWithBonuses::removeUnitBonus(const CSelector & selector)
{
	const auto guardianSpiritBonuses = getBonuses(guardianSpiritSelector());
	const bool removesGuardianSpirit = std::ranges::any_of(*guardianSpiritBonuses,
		[&](const auto & bonus) { return bonus && selector(bonus.get()); });

	// Parent models materialize fresh bonus pointers. Capture effect values before
	// suppressing them, including non-timed spells and legacy battle-long effects.
	captureEffects();
	TConstBonusListPtr toRemove = origBearer->getBonuses(selector);

	for(auto b : *toRemove)
		bonusesToRemove.insert(b);

	vstd::erase_if(bonusesToAdd, [&](const Bonus & b){return selector(&b);});
	vstd::erase_if(bonusesToUpdate, [&](const Bonus & b){return selector(&b);});
	if(projectedEffects)
		vstd::erase_if(*projectedEffects, [&](const Bonus & b){return selector(&b);});
	if(projectedUnstackedEffects)
		vstd::erase_if(*projectedUnstackedEffects, [&](const std::shared_ptr<Bonus> & b)
		{
			return !b || selector(b.get());
		});
	++treeVersionLocal;
	if(removesGuardianSpirit)
		restoreGuardianSpiritFromExistingBonuses(*this);
}

void StackWithBonuses::applyNoQuarter(int32_t moraleActivationsRemaining, bool appliedByEnemy)
{
	if(moraleActivationsRemaining <= 0 || moraleActivationsRemaining > 2)
		throw std::invalid_argument("No Quarter morale duration must be one or two activations");

	removeUnitBonus(CSelector([](const Bonus * bonus)
	{
		return newHorizonsOffense::isNoQuarterBonus(bonus);
	}));
	addUnitBonus({
		newHorizonsOffense::noQuarterRetaliationBonus(),
		newHorizonsOffense::noQuarterMoralePenalty(appliedByEnemy)});
	noQuarterMoraleActivationsRemaining = moraleActivationsRemaining;
}

void StackWithBonuses::consumeNoQuarterActivation()
{
	if(noQuarterMoraleActivationsRemaining <= 0)
		return;

	--noQuarterMoraleActivationsRemaining;
	if(noQuarterMoraleActivationsRemaining == 0)
		removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
}

void StackWithBonuses::clearNoQuarterRoundBlocker()
{
	removeUnitBonus(CSelector([](const Bonus * bonus)
	{
		return newHorizonsOffense::isNoQuarterRetaliationBonus(bonus);
	}));
}

void StackWithBonuses::captureEffects()
{
	// Resolve refreshes before aging or another mutation. In addition to spell and
	// command effects, explicitly marked affliction groups may use non-spell
	// sources; capture only those exact source/sid groups, not unrelated stats.
	const auto effects = getAllBonuses(Selector::all);
	const auto unstackedEffects = getUnstackedBonuses(Selector::all);
	projectedEffects.emplace();
	projectedUnstackedEffects.emplace();
	for(const auto & bonus : *unstackedEffects)
		if(bonus && bonus->type == BonusType::PHYSICAL_AFFLICTION)
		{
			physicalAfflictions::markerMetadata(*bonus);
			capturedPhysicalAfflictionGroups.emplace(bonus->source, bonus->sid);
		}
	for(const auto & bonus : *effects)
		if(bonus && isCapturedProjectionEffect(bonus.get(), capturedPhysicalAfflictionGroups))
			projectedEffects->push_back(*bonus);
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> rawCopies;
	for(const auto & bonus : *unstackedEffects)
		if(bonus && isCapturedProjectionEffect(bonus.get(), capturedPhysicalAfflictionGroups))
		{
			auto [copy, inserted] = rawCopies.try_emplace(bonus.get());
			if(inserted)
				copy->second = std::make_shared<Bonus>(*bonus);
			projectedUnstackedEffects->push_back(copy->second);
		}
	vstd::erase_if(bonusesToAdd, [this](const Bonus & bonus)
	{
		return isCapturedProjectionEffect(&bonus, capturedPhysicalAfflictionGroups);
	});
	vstd::erase_if(bonusesToUpdate, [this](const Bonus & bonus)
	{
		return isCapturedProjectionEffect(&bonus, capturedPhysicalAfflictionGroups);
	});
}

void StackWithBonuses::advanceTimedRound()
{
	captureEffects();
	if(isTimeStopped())
	{
		// Time Stop takes the projected unit outside the round clock.  Keep the
		// captured effect identities (so later hypothetical removals still see
		// them), but do not age or expire any N_TURNS effect.
		++treeVersionLocal;
		return;
	}
	const auto age = [this](std::vector<Bonus> & bonuses)
	{
		for(auto & bonus : bonuses)
			if(timedProjectionEffect(&bonus, capturedPhysicalAfflictionGroups) && bonus.turnsRemain > 0)
				--bonus.turnsRemain;
		vstd::erase_if(bonuses, [this](const Bonus & bonus)
		{
			return timedProjectionEffect(&bonus, capturedPhysicalAfflictionGroups) && bonus.turnsRemain <= 0;
		});
	};
	age(*projectedEffects);
	if(projectedUnstackedEffects)
	{
		// Raw snapshots may contain the same inherited Bonus pointer multiple
		// times. Clone each expiring identity once, reconnect all aliases, and age
		// that branch-local clone once so parent/sibling projections stay intact.
		std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> branchCopies;
		for(auto & bonus : *projectedUnstackedEffects)
		{
			if(!bonus || !timedProjectionEffect(bonus.get(), capturedPhysicalAfflictionGroups)
				|| bonus->turnsRemain <= 0)
				continue;
			auto [copy, inserted] = branchCopies.try_emplace(bonus.get());
			if(inserted)
				copy->second = std::make_shared<Bonus>(*bonus);
			bonus = copy->second;
		}
		for(const auto & copy : branchCopies)
			if(copy.second->turnsRemain > 0)
				--copy.second->turnsRemain;
		vstd::erase_if(*projectedUnstackedEffects, [this](const std::shared_ptr<Bonus> & bonus)
		{
			return !bonus || (timedProjectionEffect(bonus.get(), capturedPhysicalAfflictionGroups)
				&& bonus->turnsRemain <= 0);
		});
	}
	age(bonusesToAdd);
	age(bonusesToUpdate);
	++treeVersionLocal;
	restoreGuardianSpiritFromExistingBonuses(*this);
	if(health.isCapacityHealthTracking() && getCapacityHealthReferenceMax() > 0)
	{
		// Match the authoritative round-expiry hook for Hydra's Vitality. Timed
		// capacity effects disappear inside round aging rather than via a bonus
		// pack, so clamp each survivor before exposing the projected state.
		static const SpellID hydrasVitalitySpell(SpellID::decode("new-horizons:hydrasVitality"));
		const auto capacitySelector = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(hydrasVitalitySpell)).And(Selector::type()(BonusType::STACK_HEALTH));
		const auto regenerationSelector = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(hydrasVitalitySpell)).And(Selector::type()(BonusType::HP_REGENERATION));
		normalizeCapacityHealth();
		if(!hasBonus(capacitySelector) && !hasBonus(regenerationSelector))
			clearCapacityHealthReference();
	}
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

HypotheticBattle::HypotheticBattle(const Environment * ENV, Subject realBattle)
	: BattleProxy(realBattle),
	env(ENV),
	bonusTreeVersion(1)
{
	auto activeUnit = realBattle->battleActiveUnit();
	activeUnitId = activeUnit ? activeUnit->unitId() : -1;
	projectedRound = realBattle->battleGetRound();
	deploymentState = realBattle->getBattle()->getDeploymentState();
	fortuneRollRules = realBattle->getBattle()->getLuckRollRules();
	if(const auto * concreteBattle = dynamic_cast<const BattleInfo *>(realBattle->getBattle()))
		pendingTimeStopHeroActionSides = concreteBattle->getPendingTimeStopHeroActionSides();
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		projectedWalls[part] = BattleProxy::getWallState(part);
		projectedStructuralHP[part] = BattleProxy::getWallStructuralHP(part);
		canonicalStructuralHP |= projectedStructuralHP[part] > 0;
	}
	initialGateState = BattleProxy::getGateState();
	// Use the subject's visible view, not its unfiltered authoritative obstacle list.
	for(const auto & obstacle : BattleProxy::getAllObstacles())
	{
		if(const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle))
			projectedObstacles.push_back(std::make_shared<SpellCreatedObstacle>(*spell));
		else
			projectedObstacles.push_back(obstacle);
	}

	nextId = 0x00F00000;
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		reducedExtraActivationStates[side] = realBattle->getBattle()->getReducedExtraActivationState(side);
		heroOrderStates[side] = realBattle->getBattle()->getHeroOrderStates(side);
		relentlessAssaultStates[side] = realBattle->getBattle()->getRelentlessAssaultState(side);
		warcastingStates[side] = realBattle->getBattle()->getWarcastingState(side);
		heroActionAllowances[side] = realBattle->getBattle()->getHeroActionAllowances(side);
		doubleCommandStates[side] = realBattle->getBattle()->getDoubleCommandState(side);
		preCombatOrderStates[side] = realBattle->getBattle()->getPreCombatOrderState(side);
		heroSpellCastCompletedStates[side] = realBattle->getBattle()->hasCompletedHeroSpellCast(side);
		completedHeroSpellLevelMasks[side] = 0;
		for(int32_t level = 1; level <= GameConstants::SPELL_LEVELS; ++level)
			if(realBattle->getBattle()->hasCompletedHeroSpellLevel(side, level))
				completedHeroSpellLevelMasks[side] |= static_cast<std::uint8_t>(1u << (level - 1));
		counterspellArmedStates[side] = realBattle->getBattle()->getCounterspellArmed(side);
		countersequenceArmedStates[side] = realBattle->getBattle()->getMetamagicCountersequenceArmed(side);
		auto & meta = metamagicStates[side];
		meta.uses = realBattle->getBattle()->getMetamagicUsesConsumed(side);
		meta.pending = realBattle->getBattle()->getMetamagicPendingCount(side);
		meta.grandUsed = realBattle->getBattle()->getMetamagicGrandUsed(side);
		meta.firstSpell = realBattle->getBattle()->getMetamagicFirstSpell(side);
		meta.firstTarget = realBattle->getBattle()->getMetamagicFirstTargetUnitId(side);
		meta.firstCountered = realBattle->getBattle()->getMetamagicFirstCounterspellNegated(side);
		meta.sequence = realBattle->getBattle()->getMetamagicSequenceSpells(side);
		focusFireStates[side] = realBattle->battleGetFocusFireState(side);
		fortuneStates[side] = realBattle->getBattle()->getSylvanLuckState(side);
		adverseRerollStates[side] = realBattle->getBattle()->getAdverseCombatRerollState(side);
		moraleSuppressionStates[side] = realBattle->getBattle()->getMoraleSuppressionState(side);
		bloodrageRanks[side] = realBattle->getBattle()->getBloodrageRank(side);
		bloodrageDamagePercents[side] = realBattle->getBattle()->getBloodrageDamagePercent(side);
		bloodrageCaps[side] = realBattle->getBattle()->getBloodrageCapPercent(side);
		bloodrageSpeedBonuses[side] = realBattle->getBattle()->getBloodrageSpeedBonus(side);
		bloodrageAdditionalRetaliations[side] = realBattle->getBattle()->getBloodrageAdditionalRetaliations(side);
		bloodrageLowHealthIncrements[side] = realBattle->getBattle()->getBloodrageLowHealthIncrement(side);
		bloodragePainIncrements[side] = realBattle->getBattle()->getBloodragePainIncrement(side);
	}

	localEnvironment.reset(new HypotheticEnvironment(this, env));
	serverCallback.reset(new HypotheticServerCallback(this));
}

bool HypotheticBattle::hasCompletedHeroSpellLevel(BattleSide side, int32_t level) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| level < 1 || level > GameConstants::SPELL_LEVELS)
		return false;

	const auto bit = static_cast<std::uint8_t>(1u << (level - 1));
	return (completedHeroSpellLevelMasks.at(side) & bit) != 0;
}

bool HypotheticBattle::unitHasAmmoCart(const battle::Unit * unit) const
{
	return battleUnitHasAmmoCart(unit);
}

int HypotheticBattle::unitBloodragePainIncrement(const battle::Unit * unit) const
{
	return battleBloodragePainIncrement(unit);
}

std::optional<int> HypotheticBattle::unitMagicResistance(const battle::Unit * unit) const
{
	if(!unit || !newHorizonsMagic::rulesActive(getMagicRules()))
		return std::nullopt;
	return battleGetMagicResistance(unit);
}

PlayerColor HypotheticBattle::unitEffectiveOwner(const battle::Unit * unit) const
{
	return battleGetOwner(unit);
}

bool HypotheticBattle::fortuneStrikeIsCertain(const BattleAttackInfo & attack) const
{
	if(attack.luckyStrike)
		return true;

	const int luck = battleGetAttackLuck(attack.attacker, attack.defender, attack.shooting);
	if(luck == 0)
		return false;

	const auto rules = getLuckRollRules();
	if(rules.diceSize <= 0)
		return false;

	const auto & chances = luck > 0 ? rules.goodChance : rules.badChance;
	if(chances.empty())
		return false;
	const auto index = std::min<size_t>(static_cast<size_t>(std::abs(luck)), chances.size()) - 1;
	return chances[index] >= rules.diceSize;
}

ProjectedLuckOutcome HypotheticBattle::captureFortuneStrikeOutcome(const BattleAttackInfo & attack) const
{
	if(attack.luckyStrike)
		return ProjectedLuckOutcome::POSITIVE;
	if(attack.unluckyStrike)
		return ProjectedLuckOutcome::NEGATIVE;

	const int luck = battleGetAttackLuck(attack.attacker, attack.defender, attack.shooting);
	if(luck == 0)
		return ProjectedLuckOutcome::NEUTRAL;
	const auto rules = getLuckRollRules();
	if(rules.diceSize > 0)
	{
		const auto & chances = luck > 0 ? rules.goodChance : rules.badChance;
		if(!chances.empty())
		{
			const auto index = std::min<size_t>(static_cast<size_t>(std::abs(luck)), chances.size()) - 1;
			if(chances[index] <= 0)
				return ProjectedLuckOutcome::NEUTRAL;
			if(chances[index] >= rules.diceSize)
				return luck > 0 ? ProjectedLuckOutcome::POSITIVE : ProjectedLuckOutcome::NEGATIVE;
		}
	}
	return ProjectedLuckOutcome::UNKNOWN;
}

void HypotheticBattle::projectFortuneStrike(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits,
	battle::CUnitState * attackerState, bool enemyStackKilled,
	std::optional<ProjectedLuckOutcome> resolvedLuck, bool applyAftermath)
{
	const auto side = playerToSide(battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return;

	auto & fortune = fortuneStates.at(side);
	if(!fortune.active())
		return;

	const auto rules = getLuckRollRules();
	const bool gamblerAttackAvailable = fortune.gamblerAttackAvailable();
	const bool chainFortuneAvailable = fortune.chainFortuneAvailable(attack.attacker->unitId());
	// Resolve this attack with the pre-consumption Luck snapshot. Candidate
	// metadata carries that result through replay after one-strike bonuses expire.
	const auto outcome = resolvedLuck ? *resolvedLuck : captureFortuneStrikeOutcome(attack);
	const bool positive = outcome == ProjectedLuckOutcome::POSITIVE;
	const bool negative = outcome == ProjectedLuckOutcome::NEGATIVE;
	const bool guaranteedNonPositive = outcome == ProjectedLuckOutcome::NEGATIVE
		|| outcome == ProjectedLuckOutcome::NEUTRAL;
	// Chain consumes its carried benefit on the next different friendly stack's
	// attack, even when the pre-consumption Luck outcome is still stochastic.
	if(!positive && !negative && !gamblerAttackAvailable && !chainFortuneAvailable)
		return;

	// Negative Providence history is committed as well, but it has no
	// aftermath to project.  recordStrike returns true when that bad result was
	// suppressed by Providence, exactly as it does in the authoritative path.
	// Gambler and an eligible Chain gift also record unresolved strikes so the
	// candidate and committed branches consume their windows without guessing
	// the stochastic Luck result.
	const bool ignoredNegative = fortune.recordStrike(attack.attacker->unitId(), positive, negative,
		newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attack.attacker, attack.physicalDamage));
	if(gamblerAttackAvailable && guaranteedNonPositive)
	{
		static const CSelector gamblerPenaltySelector([](const Bonus * bonus)
		{
			return newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		});
		auto projectedAttacker = getForUpdate(attack.attacker->unitId());
		if(!projectedAttacker->hasBonus(gamblerPenaltySelector))
			addUnitBonus(attack.attacker->unitId(), {newHorizonsCombatSkills::gamblerLuckPenalty()});
	}
	if(ignoredNegative || !applyAftermath)
		return;
	if(!positive)
		return;

	std::vector<uint32_t> adjacentFriends = battleFortuneAdjacentFriends(attack.attacker);
	int64_t actualDamage = 0;
	for(const auto & [unitId, damage] : hits)
	{
		const bool primary = unitId == attack.defender->unitId();
		if(!primary && !rules.affectsAllTargets)
			continue;
		actualDamage += std::max<int64_t>(0, damage);
		const auto * target = battleGetUnitByID(unitId);
		if(target && !target->alive())
			vstd::erase(adjacentFriends, unitId);
	}

	fortune.finishPositiveStrike(adjacentFriends, enemyStackKilled);
	if(!attack.shooting && fortune.luckyRecovery && attackerState && attackerState->alive())
	{
		auto healing = SylvanLuckState::recoveryAmount(actualDamage);
		attackerState->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	}
}

std::shared_ptr<StackWithBonuses> HypotheticBattle::getForUpdate(uint32_t id)
{
	auto iter = stackStates.find(id);

	if(iter == stackStates.end())
	{
		const battle::Unit * s = subject->battleGetUnitByID(id);

		auto ret = std::make_shared<StackWithBonuses>(this, s);
		stackStates[id] = ret;
		return ret;
	}
	else
	{
		return iter->second;
	}
}

battle::Units HypotheticBattle::getUnitsIf(const battle::UnitFilter & predicate) const
{
	const auto original = BattleProxy::getUnitsIf([](const battle::Unit *)
	{
		return true;
	});
	battle::Units result;
	result.reserve(original.size() + stackStates.size());
	std::set<uint32_t> originalIds;
	for(const auto * unit : original)
	{
		originalIds.insert(unit->unitId());
		const auto replacement = stackStates.find(unit->unitId());
		const auto * current = replacement == stackStates.end() ? unit : replacement->second.get();
		if(predicate(current))
			result.push_back(current);
	}
	// Preserve real battle order for replacements; append only newly projected units.
	for(const auto & [id, unit] : stackStates)
		if(!originalIds.contains(id) && predicate(unit.get()))
			result.push_back(unit.get());
	return result;
}

BattleID HypotheticBattle::getBattleID() const
{
	return subject->getBattle()->getBattleID();
}

ui8 HypotheticBattle::getTacticDist() const
{
	if(deploymentState.independent)
	{
		return deploymentState.activeDistance();
	}
	return BattleProxy::getTacticDist();
}

BattleSide HypotheticBattle::getTacticsSide() const
{
	if(deploymentState.independent)
		return deploymentState.activeSide();
	return BattleProxy::getTacticsSide();
}

void HypotheticBattle::setDeploymentState(const BattleDeploymentState & state)
{
	state.validateTransitionFrom(deploymentState);
	deploymentState = state;
}

const ReducedExtraActivationState & HypotheticBattle::getReducedExtraActivationState(BattleSide side) const
{
	return reducedExtraActivationStates.at(side);
}

void HypotheticBattle::setReducedExtraActivationState(BattleSide side,
	const ReducedExtraActivationState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical reduced extra activation side");
	state.validateShape();
	reducedExtraActivationStates.at(side) = state;
}

void HypotheticBattle::setPreCombatOrderState(BattleSide side, const PreCombatOrderState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Battle Plan side");
	state.validateShape();
	preCombatOrderStates.at(side) = state;
}

std::vector<HeroOrderState> HypotheticBattle::getHeroOrderStates(BattleSide side) const
{
	return heroOrderStates.at(side);
}

std::optional<HeroOrderState> HypotheticBattle::getHeroOrderState(BattleSide side, HeroCommand command) const
{
	const auto & states = heroOrderStates.at(side);
	const auto found = std::find_if(states.begin(), states.end(), [command](const HeroOrderState & state)
	{
		return state.command == command;
	});
	return found == states.end() ? std::optional<HeroOrderState>() : *found;
}

std::optional<HeroOrderState> HypotheticBattle::getHeroOrderState(BattleSide side) const
{
	const auto & states = heroOrderStates.at(side);
	return states.empty() ? std::optional<HeroOrderState>() : states.back();
}

std::vector<HeroOrderState> HypotheticBattle::battleGetHeroOrderStates(BattleSide side) const
{
	return getHeroOrderStates(side);
}

std::optional<HeroOrderState> HypotheticBattle::battleGetHeroOrderState(BattleSide side,
	HeroCommand command) const
{
	return getHeroOrderState(side, command);
}

std::optional<HeroOrderState> HypotheticBattle::battleGetHeroOrderState(BattleSide side) const
{
	return getHeroOrderState(side);
}

HeroCommand HypotheticBattle::getActiveOrder(BattleSide side) const
{
	if(!heroCommands::isCanonicalRules(getHeroCommandRules()))
		return BattleProxy::getActiveOrder(side);
	const auto & states = heroOrderStates.at(side);
	return states.empty() ? HeroCommand::NONE : states.back().command;
}

const RelentlessAssaultState & HypotheticBattle::battleGetRelentlessAssaultState(BattleSide side) const
{
	return relentlessAssaultStates.at(side);
}

const RelentlessAssaultState & HypotheticBattle::getRelentlessAssaultState(BattleSide side) const
{
	return relentlessAssaultStates.at(side);
}

void HypotheticBattle::setRelentlessAssaultState(BattleSide side, const RelentlessAssaultState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Relentless Assault side");
	state.validateShape();
	const auto * hero = battleGetFightingHero(side);
	if((!hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
		&& state.hasState())
		throw std::runtime_error("Hypothetical Relentless Assault state requires its hero perk");
	relentlessAssaultStates.at(side) = state;
}

void HypotheticBattle::recordRelentlessAssaultAttack(BattleSide side, uint32_t targetUnitId)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return;
	const auto * hero = battleGetFightingHero(side);
	if(!hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
		return;
	relentlessAssaultStates.at(side).recordAttack(targetUnitId);
}

const AlternatingHeroActionState & HypotheticBattle::getWarcastingState(BattleSide side) const
{
	return warcastingStates.at(side);
}

const HeroActionAllowanceState & HypotheticBattle::getHeroActionAllowances(BattleSide side) const
{
	return heroActionAllowances.at(side);
}

std::optional<HypotheticBattle::ProjectedSpellAllowance> HypotheticBattle::prepareHeroSpellAllowance(
	BattleSide side, bool metamagicFollowup, bool grand) const
{
	const auto & ledger = heroActionAllowances.at(side);
	if(ledger.currentRound < 0)
	{
		// Legacy battles have no typed ledger. Preserve their historical distinction:
		// only an ordinary cast spends the flexible Hero Action.
		ProjectedActionReceipt action;
		action.side = side;
		action.typedLedger = false;
		action.epoch = projectedActionEpochs.at(side);
		action.receipt = {0, HeroActionAllowanceState::ActionKind::SPELL,
			metamagicFollowup ? HeroActionAllowanceState::AllowanceKind::SPELL
				: HeroActionAllowanceState::AllowanceKind::HERO,
			metamagicFollowup ? HeroActionAllowanceState::GrantSource::METAMAGIC
				: HeroActionAllowanceState::GrantSource::OTHER,
			projectedRound};
		ProjectedSpellAllowance prepared;
		prepared.action = action;
		prepared.allowancesBefore = ledger;
		prepared.allowancesAfter = ledger;
		prepared.metamagicBefore = metamagicStates.at(side);
		prepared.metamagicFollowup = metamagicFollowup;
		prepared.grand = grand;
		prepared.usesAfter = metamagicStates.at(side).uses;
		prepared.pendingAfter = metamagicStates.at(side).pending;
		prepared.grandUsedAfter = metamagicStates.at(side).grandUsed;
		return prepared;
	}

	try
	{
		auto nextLedger = ledger;
		auto meta = metamagicStates.at(side);
		if(nextLedger.currentRound != projectedRound)
			return {};
		const auto selection = nextLedger.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL, projectedRound);
		if(!selection)
			return {};
		const auto * hero = getSideHero(side);
		const auto transition = HeroSpellAllowanceTransition::commitAcceptedCast(nextLedger, selection->grantId,
			projectedRound, metamagicFollowup, grand,
			hero ? newHorizonsMagic::metamagicRank(hero) : 0,
			hero && newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
			meta.uses, meta.pending, meta.grandUsed, meta.sequence.size());
		if(!transition)
			return {};

		ProjectedSpellAllowance prepared;
		prepared.action = {side, transition->receipt, true, projectedActionEpochs.at(side)};
		prepared.allowancesBefore = ledger;
		prepared.allowancesAfter = std::move(nextLedger);
		prepared.metamagicBefore = metamagicStates.at(side);
		prepared.metamagicFollowup = metamagicFollowup;
		prepared.grand = grand;
		prepared.usesAfter = meta.uses;
		prepared.pendingAfter = meta.pending;
		prepared.grandUsedAfter = meta.grandUsed;
		return prepared;
	}
	catch(const std::exception &)
	{
		// Malformed or stale candidate state is rejected before any projected effect
		// (including action-boundary cleanup) can touch this clone.
		return {};
	}
}

std::optional<HypotheticBattle::ProjectedOrderAllowance> HypotheticBattle::prepareHeroOrderAllowance(
	BattleSide side) const
{
	const auto & ledger = heroActionAllowances.at(side);
	if(ledger.currentRound < 0)
	{
		ProjectedActionReceipt action;
		action.side = side;
		action.typedLedger = false;
		action.epoch = projectedActionEpochs.at(side);
		action.receipt = {0, HeroActionAllowanceState::ActionKind::ORDER,
			HeroActionAllowanceState::AllowanceKind::HERO,
			HeroActionAllowanceState::GrantSource::OTHER, projectedRound};
		return ProjectedOrderAllowance{action, ledger, ledger, metamagicStates.at(side)};
	}

	try
	{
		auto nextLedger = ledger;
		if(nextLedger.currentRound != projectedRound)
			return {};
		const auto selection = nextLedger.eligibleAllowance(HeroActionAllowanceState::ActionKind::ORDER, projectedRound);
		if(!selection)
			return {};
		if(selection->allowance == HeroActionAllowanceState::AllowanceKind::HERO
			&& nextLedger.nextGrantId == std::numeric_limits<uint32_t>::max()
			&& !doubleCommandStates.at(side).used
			&& heroCommands::hasDoubleCommand(battleGetFightingHero(side)))
			return {};
		const auto receipt = nextLedger.consumeAllowance(selection->grantId,
			HeroActionAllowanceState::ActionKind::ORDER, projectedRound);
		if(!receipt)
			return {};
		return ProjectedOrderAllowance{{side, *receipt, true, projectedActionEpochs.at(side)},
			ledger, std::move(nextLedger), metamagicStates.at(side)};
	}
	catch(const std::exception &)
	{
		return {};
	}
}

bool HypotheticBattle::isCurrentPreparedSpellAction(BattleSide side,
	const ProjectedSpellAllowance & prepared, bool requireBegun) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| prepared.action.side != side
		|| prepared.action.epoch != projectedActionEpochs.at(side)
		|| prepared.action.receipt.round != projectedRound
		|| prepared.metamagicBefore != metamagicStates.at(side))
		return false;
	const auto & begunSpell = begunProjectedSpellAllowances.at(side);
	const auto & begunOrder = begunProjectedOrderAllowances.at(side);
	if((requireBegun && (!begunSpell || *begunSpell != prepared || begunOrder.has_value()))
		|| (!requireBegun && (begunSpell.has_value() || begunOrder.has_value())))
		return false;
	const auto current = prepareHeroSpellAllowance(side, prepared.metamagicFollowup, prepared.grand);
	return current && *current == prepared;
}

bool HypotheticBattle::isCurrentPreparedOrderAction(BattleSide side,
	const ProjectedOrderAllowance & prepared, bool requireBegun) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| prepared.action.side != side
		|| prepared.action.epoch != projectedActionEpochs.at(side)
		|| prepared.action.receipt.round != projectedRound
		|| prepared.metamagicBefore != metamagicStates.at(side))
		return false;
	const auto & begunSpell = begunProjectedSpellAllowances.at(side);
	const auto & begunOrder = begunProjectedOrderAllowances.at(side);
	if((requireBegun && (!begunOrder || *begunOrder != prepared || begunSpell.has_value()))
		|| (!requireBegun && (begunSpell.has_value() || begunOrder.has_value())))
		return false;
	const auto current = prepareHeroOrderAllowance(side);
	return current && *current == prepared;
}

bool HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared)
{
	if(!isCurrentPreparedSpellAction(side, prepared, false))
		return false;
	begunProjectedSpellAllowances.at(side) = prepared;
	beginProjectedHeroAction(side, prepared.action);
	return true;
}

bool HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	if(!isCurrentPreparedOrderAction(side, prepared, false))
		return false;
	begunProjectedOrderAllowances.at(side) = prepared;
	beginProjectedHeroAction(side, prepared.action);
	return true;
}

void HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedActionReceipt & action)
{
	if(action.side != side || action.epoch != projectedActionEpochs.at(side))
		return;
	// The complete typed plan was revalidated and recorded before this cleanup
	// boundary; commit must present that same plan, not another same-epoch action.
	if(!action.isHeroAction())
		return;

	expireProjectedTimeStops(side);
	counterspellArmedStates.at(side) = false;
	countersequenceArmedStates.at(side) = false;
}

void HypotheticBattle::finishProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared)
{
	if(begunProjectedSpellAllowances.at(side)
		&& *begunProjectedSpellAllowances.at(side) == prepared)
	{
		begunProjectedSpellAllowances.at(side).reset();
		++projectedActionEpochs.at(side);
	}
}

void HypotheticBattle::finishProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	if(begunProjectedOrderAllowances.at(side)
		&& *begunProjectedOrderAllowances.at(side) == prepared)
	{
		begunProjectedOrderAllowances.at(side).reset();
		++projectedActionEpochs.at(side);
	}
}

void HypotheticBattle::expireProjectedTimeStops(BattleSide casterSide)
{
	const auto sideMask = timeStopSideMask(casterSide);
	if(!sideMask)
		return;

	for(const auto * unit : getUnitsIf([](const battle::Unit *) { return true; }))
	{
		auto projected = getForUpdate(unit->unitId());
		projected->removeUnitBonus(CSelector([casterSide](const Bonus * bonus)
		{
			return bonus && timeStopState::isStateBonus(*bonus)
				&& timeStopState::belongsToSide(*bonus, casterSide);
		}));
	}
	pendingTimeStopHeroActionSides &= static_cast<ui8>(~sideMask);
	++bonusTreeVersion;
}

bool HypotheticBattle::projectAcceptedHeroSpell(BattleSide side, SpellID spell, uint32_t target,
	bool metamagicFollowup, bool grand, bool counterspellWardActive, bool counterspellNegated,
	const ProjectedSpellAllowance & prepared)
{
	const auto & action = prepared.action;
	if(action.receipt.action != HeroActionAllowanceState::ActionKind::SPELL
		|| metamagicFollowup != prepared.metamagicFollowup || grand != prepared.grand
		|| !isCurrentPreparedSpellAction(side, prepared, true))
		return false;

	auto & meta = metamagicStates.at(side);
	if(action.typedLedger)
	{
		heroActionAllowances.at(side) = prepared.allowancesAfter;
		meta.uses = prepared.usesAfter;
		meta.pending = prepared.pendingAfter;
		meta.grandUsed = prepared.grandUsedAfter;

		if(action.receipt.allowance == HeroActionAllowanceState::AllowanceKind::HERO)
		{
			if(meta.pending != 0)
			{
				meta.firstSpell = spell;
				meta.firstTarget = target;
				meta.firstCountered = counterspellNegated;
				meta.sequence = {spell};
			}
			else
			{
				meta.firstSpell = SpellID();
				meta.firstTarget = std::numeric_limits<uint32_t>::max();
				meta.firstCountered = false;
				meta.sequence.clear();
			}
		}
		else if(action.receipt.source == HeroActionAllowanceState::GrantSource::METAMAGIC
			|| action.receipt.source == HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND)
		{
			meta.sequence.push_back(spell);
			if(meta.pending == 0)
			{
				meta.firstSpell = SpellID();
				meta.firstTarget = std::numeric_limits<uint32_t>::max();
				meta.firstCountered = false;
				meta.sequence.clear();
			}
		}
	}

	const auto * hero = getSideHero(side);
	if(action.isHeroAction() && newHorizonsWarcasting::enabled(getMagicRules()))
		warcastingStates.at(side).recordAcceptedAction(AlternatingHeroActionState::Action::SPELL,
			projectedRound, newHorizonsWarcasting::empowerment(hero, AlternatingHeroActionState::Action::SPELL),
			newHorizonsWarcasting::readinessLifetimeRounds(hero));

	const auto enemySide = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	if(counterspellWardActive)
	{
		counterspellArmedStates.at(enemySide) = false;
		countersequenceArmedStates.at(enemySide) = false;
	}
	if(!counterspellNegated && newHorizonsMagic::isCounterspell(spell.toSpell()))
	{
		counterspellArmedStates.at(side) = true;
		countersequenceArmedStates.at(side) = metamagicFollowup && newHorizonsMagic::hasMetamagicPerk(
			hero, newHorizonsMagic::METAMAGIC_COUNTERSEQUENCE);
	}
	finishProjectedHeroAction(side, prepared);
	return true;
}

bool HypotheticBattle::projectAcceptedHeroOrder(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	return projectAcceptedHeroOrder(side, HeroCommand::NONE, {}, prepared);
}

bool HypotheticBattle::projectAcceptedHeroOrder(BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & commandTargets, const ProjectedOrderAllowance & prepared)
{
	const auto & action = prepared.action;
	if(action.receipt.action != HeroActionAllowanceState::ActionKind::ORDER
		|| !isCurrentPreparedOrderAction(side, prepared, true))
		return false;
	const bool projectingCommand = command != HeroCommand::NONE;
	if(projectingCommand && !heroCommands::isActive(command))
		return false;
	auto & doubleCommand = doubleCommandStates.at(side);
	auto & preCombatOrder = preCombatOrderStates.at(side);
	const bool resolvingDoubleCommand = doubleCommand.orderPending();
	const bool resolvingPreCombatOrder = preCombatOrder.orderPending();
	if(resolvingDoubleCommand && resolvingPreCombatOrder)
		return false;
	if(resolvingPreCombatOrder)
	{
		const auto * active = activeUnitId >= 0
			? battleGetUnitByID(static_cast<uint32_t>(activeUnitId)) : nullptr;
		if(!projectingCommand || !prepared.action.typedLedger
			|| action.receipt.source != HeroActionAllowanceState::GrantSource::BATTLE_PLAN
			|| !active || !active->alive() || active->isGhost() || active->isTurret()
			|| active->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| active->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| active->unitSlot() == SlotID::WAR_MACHINES_SLOT
			|| preCombatOrder.issuedRound != projectedRound || projectedRound != 1
			|| preCombatOrder.anchorStackId != active->unitId()
			|| battleGetOwner(active) != sideToPlayer(side))
			return false;
		auto completed = preCombatOrder;
		completed.complete();
		completed.validateTransitionFrom(preCombatOrder);
	}
	else if(action.receipt.source == HeroActionAllowanceState::GrantSource::BATTLE_PLAN)
		return false;
	if(resolvingDoubleCommand)
	{
		const auto * active = activeUnitId >= 0
			? battleGetUnitByID(static_cast<uint32_t>(activeUnitId)) : nullptr;
		if(!projectingCommand || !prepared.action.typedLedger
			|| action.receipt.source != HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND
			|| !active || !active->alive() || active->isGhost()
			|| doubleCommand.issuedRound != projectedRound
			|| doubleCommand.anchorStackId != active->unitId()
			|| battleGetOwner(active) != sideToPlayer(side)
			|| command == doubleCommand.firstOrder)
			return false;
	}
	else if(projectingCommand && action.receipt.source == HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND)
		return false;
	if(action.typedLedger)
		heroActionAllowances.at(side) = prepared.allowancesAfter;
	if(action.isHeroAction() && newHorizonsWarcasting::enabled(getMagicRules()))
	{
		const auto * hero = getSideHero(side);
		warcastingStates.at(side).recordAcceptedAction(AlternatingHeroActionState::Action::ORDER,
			projectedRound, newHorizonsWarcasting::empowerment(hero, AlternatingHeroActionState::Action::ORDER),
			newHorizonsWarcasting::readinessLifetimeRounds(hero));
	}
	if(resolvingPreCombatOrder)
		preCombatOrder.complete();
	else if(resolvingDoubleCommand)
		doubleCommand.completeFollowup(command);
	else if(projectingCommand && action.typedLedger && action.isHeroAction())
	{
		const auto * hero = battleGetFightingHero(side);
		const auto * active = activeUnitId >= 0
			? battleGetUnitByID(static_cast<uint32_t>(activeUnitId)) : nullptr;
		const auto anchor = active && active->alive() && !active->isGhost()
			&& battleGetOwner(active) == sideToPlayer(side)
			? active->unitId() : DoubleCommandState::INVALID_UNIT_ID;
		const uint32_t deferredTarget = command == HeroCommand::SECOND_WIND && commandTargets.size() == 1
			? commandTargets.front() : DoubleCommandState::INVALID_UNIT_ID;
		if(doubleCommand.begin(action.receipt, heroCommands::hasDoubleCommand(hero), command,
			projectedRound, anchor, deferredTarget))
		{
			auto & allowances = heroActionAllowances.at(side);
			allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
				HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND, projectedRound);

			// Match the authoritative no-alternative path: do not leave an orphaned
			// grant or a pending phase when the primary Order exhausted every other
			// legal Order for this battle state.
			const bool hasDistinctAlternative = std::any_of(heroCommands::CANONICAL_COMMANDS.begin(),
				heroCommands::CANONICAL_COMMANDS.end(), [this, side, command](HeroCommand candidate)
				{
					return candidate != command && battleCanBeginHeroCommand(side, candidate);
				});
			if(!hasDistinctAlternative)
			{
				doubleCommand.exhaustPendingOrder();
				std::erase_if(allowances.grants, [this](const HeroActionAllowanceState::Grant & grant)
				{
					return grant.source == HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND
						&& grant.grantedRound == projectedRound;
				});
			}
		}
	}
	finishProjectedHeroAction(side, prepared);
	return true;
}

HypotheticBattle::ProjectedCounterspellOutcome HypotheticBattle::resolveProjectedCounterspell(
	BattleSide casterSide, const CSpell * spell) const
{
	ProjectedCounterspellOutcome result;
	if(!spell || (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return result;

	const auto wardSide = casterSide == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	result.resolutionKnown = true;
	result.negated = false;
	if(!counterspellArmedStates.at(wardSide))
		return result;

	// The ward flag is public battle state, but an ordinary player callback may
	// deliberately hide the opposing hero's mana and Countermage selection.
	// Preserve the known ward while leaving its cost/negation unresolved.
	result.wardSide = wardSide;
	result.wardActive = true;
	result.resolutionKnown = false;
	result.negated.reset();
	const auto * caster = getSideHero(casterSide);
	const auto * wardingHero = getSideHero(wardSide);
	if(!caster || !wardingHero)
		return result;

	const int listedCost = caster->getListedSpellCost(spell);
	const int manaCost = newHorizonsMagic::counterspellCost(listedCost,
		wardingHero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.countermage"),
		countersequenceArmedStates.at(wardSide));
	result.manaCost = manaCost;
	result.negated = wardingHero->getManaAvailable() >= manaCost;
	result.resolutionKnown = true;
	return result;
}

bool HypotheticBattle::projectHeroSpellAllowance(BattleSide side, SpellID spell, uint32_t target,
	bool metamagicFollowup, bool grand)
{
	const auto prepared = prepareHeroSpellAllowance(side, metamagicFollowup, grand);
	if(!prepared)
		return false;
	const auto counterspell = resolveProjectedCounterspell(side, spell.toSpell());
	if(!counterspell.resolutionKnown || !counterspell.negated.has_value())
		return false;
	if(!beginProjectedHeroAction(side, *prepared))
		return false;
	return projectAcceptedHeroSpell(side, spell, target, metamagicFollowup, grand,
		counterspell.wardActive, *counterspell.negated, *prepared);
}

bool HypotheticBattle::projectHeroOrderAllowance(BattleSide side)
{
	const auto prepared = prepareHeroOrderAllowance(side);
	if(!prepared || !beginProjectedHeroAction(side, *prepared))
		return false;
	return projectAcceptedHeroOrder(side, *prepared);
}

void HypotheticBattle::setHeroOrderStates(BattleSide side, const std::vector<HeroOrderState> & states)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Hero Order side");
	SideInBattle::validateOrderCollection(states);
	if(heroOrderStates.at(side) == states)
		return;
	heroOrderStates.at(side) = states;
	++bonusTreeVersion;
}

void HypotheticBattle::setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Hero Order side");
	if(!state)
	{
		setHeroOrderStates(side, {});
		return;
	}
	state->validateShape();
	auto states = heroOrderStates.at(side);
	const auto found = std::find_if(states.begin(), states.end(), [&](const HeroOrderState & current)
	{
		return current.command == state->command;
	});
	if(found == states.end())
		states.push_back(*state);
	else if(*found == *state)
		return;
	else
		*found = *state;
	setHeroOrderStates(side, states);
}

bool HypotheticBattle::consumeHeroOrderProtectInterception(uint32_t wardUnitId, uint32_t protectorUnitId)
{
	const auto * ward = battleGetUnitByID(wardUnitId);
	if(!ward)
		return false;
	const auto side = ward->unitSide();
	auto state = battleGetHeroOrderState(side, HeroCommand::PROTECT);
	if(!state || state->issuedRound != battleGetRound()
		|| state->secondaryTargetUnitId != wardUnitId || state->primaryTargetUnitId != protectorUnitId
		|| state->protectBroken
		|| state->protectInterceptionsConsumed >= battleHeroOrderProtectInterceptionLimit(side))
		return false;
	++state->protectInterceptionsConsumed;
	setHeroOrderState(side, state);
	return true;
}

std::optional<FocusFireState> HypotheticBattle::getFocusFireState(BattleSide side) const
{
	const auto found = focusFireStates.find(side);
	return found == focusFireStates.end() ? std::optional<FocusFireState>() : found->second;
}

void HypotheticBattle::setFocusFireState(BattleSide side, const FocusFireState & state)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !heroCommands::supportedByRules(getHeroCommandRules(), HeroCommand::FOCUS_FIRE))
		throw std::invalid_argument("Invalid hypothetical Focus Fire context");
	state.validateShape();
	if(state.issuedRound != battleGetRound())
		throw std::invalid_argument("Invalid hypothetical Focus Fire round");
	focusFireStates[side] = state;
	++bonusTreeVersion;
}

int32_t HypotheticBattle::getActiveStackID() const
{
	return activeUnitId;
}

float HypotheticBattle::projectMoraleActivationDelta(const battle::Unit * original,
	const battle::Unit * projected, float before, float after, float horizon)
{
	const float delta = after - before;
	if(!original || !projected || horizon <= 0.0f || delta == 0.0f)
		return delta * std::max(0.0f, horizon);
	const auto side = playerToSide(battleGetOwner(projected));
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| playerToSide(battleGetOwner(original)) != side)
		return delta * horizon;
	auto & suppression = moraleSuppressionStates.at(side);
	if(!suppression.consume(before < 0.0f || after < 0.0f))
		return delta * horizon;

	// Approximate one prospective activation in this candidate branch. The rest
	// of the horizon remains exposed; actual first-trigger ordering is unknown.
	const float protectedDelta = std::max(0.0f, after) - std::max(0.0f, before);
	return delta * horizon + (protectedDelta - delta) * std::min(1.0f, horizon);
}

int32_t HypotheticBattle::getRound() const
{
	return projectedRound;
}

int32_t HypotheticBattle::getBloodrageDamagePercent(BattleSide side) const
{
	return bloodrageDamagePercents.at(side);
}

int32_t HypotheticBattle::getBloodrageCapPercent(BattleSide side) const
{
	return bloodrageCaps.at(side);
}

int32_t HypotheticBattle::getBloodrageSpeedBonus(BattleSide side) const
{
	return bloodrageSpeedBonuses.at(side);
}

int32_t HypotheticBattle::getBloodrageAdditionalRetaliations(BattleSide side) const
{
	return bloodrageAdditionalRetaliations.at(side);
}

int32_t HypotheticBattle::getBloodrageLowHealthIncrement(BattleSide side) const
{
	return bloodrageLowHealthIncrements.at(side);
}

int32_t HypotheticBattle::getBloodragePainIncrement(BattleSide side) const
{
	return bloodragePainIncrements.at(side);
}

IBattleInfo::ObstacleCList HypotheticBattle::getAllObstacles() const
{
	return projectedObstacles;
}

void HypotheticBattle::nextRound()
{
	if(deploymentState.independent && deploymentState.activeSide() != BattleSide::NONE)
		throw std::runtime_error("Cannot advance a battle round with unresolved deployment phase");
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(preCombatOrderStates.at(side).isUnresolved())
			throw std::runtime_error("Cannot advance a battle round with unresolved Battle Plan choice");
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(doubleCommandStates.at(side).orderPending()
			|| doubleCommandStates.at(side).secondWindReady())
			throw std::runtime_error("Cannot advance a battle round with unresolved Double Command continuation");
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		fortuneStates[side].nextRound();
		moraleSuppressionStates[side].nextRound();
		heroOrderStates[side].clear();
	}
	for(auto & [side, state] : focusFireStates)
		state.reset();
	++bonusTreeVersion;
	// BattleInfo grants opening effects their full duration in round one.
	const bool firstRound = projectedRound == 0;
	++projectedRound;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		warcastingStates[side] = warcastingStates[side].clearedIfExpired(projectedRound);
		if(heroActionAllowances[side].currentRound >= 0)
			heroActionAllowances[side].resetForRound(projectedRound);
		auto & meta = metamagicStates[side];
		meta.pending = 0;
		meta.firstSpell = SpellID();
		meta.firstTarget = std::numeric_limits<uint32_t>::max();
		meta.firstCountered = false;
		meta.sequence.clear();
	}
	std::vector<uint32_t> pendingRemoval;
	for(const auto * unit : getUnitsIf([](const battle::Unit *) { return true; }))
	{
		auto forUpdate = getForUpdate(unit->unitId());
		forUpdate->clearNoQuarterRoundBlocker();
		if(!firstRound && !forUpdate->isTimeStopped())
			forUpdate->advanceTimedRound();
		forUpdate->afterNewRound(firstRound);
		if(forUpdate->ghostPending)
			pendingRemoval.push_back(unit->unitId());
	}
	// The authoritative flow flushes pending ghosts after round updates. This
	// also releases originals whose clones lost their duration marker.
	for(const auto id : pendingRemoval)
		removeUnit(id);
	// Unlike opening unit enchantments, obstacle timers tick on every round
	// transition. Authoritative BattleFlowProcessor then removes zero timers.
	for(auto & obstacle : projectedObstacles)
	{
		if(const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle))
		{
			auto aged = std::make_shared<SpellCreatedObstacle>(*spell);
			aged->battleTurnPassed();
			obstacle = aged;
		}
	}
	const auto previousSize = projectedObstacles.size();
	vstd::erase_if(projectedObstacles, [](const auto & obstacle)
	{
		const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
		return spell && spell->turnsRemaining == 0;
	});
	obstacleChanges |= previousSize != projectedObstacles.size();
}

void HypotheticBattle::nextTurn(uint32_t unitId, BattleUnitTurnReason reason)
{
	activeUnitId = unitId;
	auto unit = getForUpdate(unitId);
	if(reason == BattleUnitTurnReason::ACTION_REJECTED
		|| reason == BattleUnitTurnReason::MASTER_GATE_CONTINUATION
		|| reason == BattleUnitTurnReason::PURSUIT_CONTINUATION)
		return;
	if(battleBeginsActivation(unit.get(), reason))
	{
		unit->removeUnitBonus(CSelector(Bonus::UntilNextCreatureActivation));

		// STACK_GETS_TURN is deliberately not globally broadened for HERO_COMMAND
		// transitions. Gambler's own penalty ends on every genuine activation,
		// including Second Wind, without expiring unrelated bonuses early.
		unit->removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		}));

		const auto activationSide = playerToSide(battleGetOwner(unit.get()));
		const auto * veteranHero = activationSide == BattleSide::ATTACKER || activationSide == BattleSide::DEFENDER
			? battleGetFightingHero(activationSide) : nullptr;
		newHorizonsCombatSkills::applyVeteran(unit.get(), veteranHero);

		// Match the authoritative BattleFlowProcessor activation-start order:
		// Regeneration consumes only marks already recorded for this survivor,
		// before poison or any other start-of-activation damage can occur.
		if(unit->alive() && !unit->isTimeStopped() && unit->regenerationPendingMicroHealth > 0)
		{
			auto healing = unit->consumeRegenerationMarks();
			if(healing > 0)
				unit->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
		}
		static const SpellID hydrasVitalitySpell(SpellID::decode("new-horizons:hydrasVitality"));
		const auto hydrasVitalityMarker = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(hydrasVitalitySpell)).And(Selector::type()(BonusType::HP_REGENERATION));
		if(unit->alive() && !unit->isTimeStopped()
			&& unit->getCapacityHealthReferenceMax() > 0
			&& unit->health.isCapacityHealthTracking()
			&& unit->hasBonus(hydrasVitalityMarker))
			unit->consumeCapacityRegeneration();

		auto poisonDamage = newHorizonsBulwark::physicalPoisonTickDamage(unit.get());
		if(poisonDamage > 0)
		{
			unit->damage(poisonDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
			newHorizonsBulwark::advancePhysicalPoison(unit.get());
		}
		if(unit->bulwarkMireGripApplied)
		{
			const int bulwarkSkillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
			if(bulwarkSkillId >= 0)
				unit->removeUnitBonus(Selector::source(BonusSource::OTHER,
					BonusSourceID(SecondarySkill(bulwarkSkillId))));
			unit->bulwarkMireGripApplied = false;
		}
		if(unit->alive())
			newHorizonsBulwark::applySwampRenewal(unit.get(), battleGetOwnerHero(unit.get()));
		const auto side = playerToSide(battleGetOwner(unit.get()));
		for(auto owner : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			fortuneStates.at(owner).beginActivation(unitId, side == owner);
	}
	bool newActivation = reason != BattleUnitTurnReason::HERO_SPELLCAST
		&& reason != BattleUnitTurnReason::UNIT_SPELLCAST;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		newActivation = false;
		if(unit)
		{
			const auto controllerSide = playerToSide(battleGetOwner(unit.get()));
			if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
			{
				const auto orderState = getHeroOrderState(controllerSide, HeroCommand::SECOND_WIND);
				newActivation = orderState
					&& orderState->secondWindActive
					&& orderState->primaryTargetUnitId == unitId;
			}
		}
	}
	if(newActivation)
	{
		unit->pursuitMovementRemaining = 0;
		unit->cleaveUsedThisActivation = false;
		const auto side = playerToSide(battleGetOwner(unit.get()));
		const bool ordinaryCreature = unit->alive() && !unit->isGhost() && !unit->isTurret()
			&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
		const auto * hero = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
			? battleGetFightingHero(side) : nullptr;
		if(ordinaryCreature && hero
			&& hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT))
			relentlessAssaultStates.at(side).beginActivation();
	}

	if(!unit->isTimeStopped() && reason != BattleUnitTurnReason::UNIT_SPELLCAST && reason != BattleUnitTurnReason::HERO_COMMAND)
		unit->removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return Bonus::UntilGetsTurn(bonus)
				&& !newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		}));

	if(battleBeginsActivation(unit.get(), reason))
		unit->setActivationMovementBonus(newHorizonsBattlecraft::delayedActivationMovementBonus(
			battleGetOwnerHero(unit.get()), unit.get(), reason));

	unit->afterGetsTurn(reason);
}

void HypotheticBattle::addUnit(uint32_t id, const JsonNode & data)
{
	battle::UnitInfo info;
	info.load(id, data);
	auto newUnit = std::make_shared<StackWithBonuses>(this, info);
	stackStates[newUnit->unitId()] = newUnit;
	const auto orderState = getHeroOrderState(info.side, HeroCommand::RIPOSTE);
	const auto * hero = battleGetFightingHero(info.side);
	if(orderState && orderState->issuedRound == projectedRound
		&& hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE)
		&& newUnit->alive() && !newUnit->isGhost() && !newUnit->isTurret()
		&& !newUnit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& newUnit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
		&& !newHorizonsOffense::hasVengeanceRetaliationBonus(newUnit.get()))
		addUnitBonus(newUnit->unitId(), {newHorizonsOffense::vengeanceRetaliationBonus()});
}

void HypotheticBattle::moveUnit(uint32_t id, const BattleHex & destination)
{
	std::shared_ptr<StackWithBonuses> changed = getForUpdate(id);
	const bool moved = changed->position != destination;
	if(moved && changed->hasBonusOfType(BonusType::BIND_EFFECT))
	{
		if(const auto entangleSpell = entangleSpellId())
			changed->removeUnitBonus(Selector::type()(BonusType::BIND_EFFECT)
				.And(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(*entangleSpell))));
	}
	changed->position = destination;
	if(!moved)
		return;

	const auto unitsAdjacent = [](const battle::Unit * first, const battle::Unit * second)
	{
		if(!first || !second)
			return false;
		for(const auto & firstHex : first->getHexes())
		{
			if(!firstHex.isValid())
				continue;
			for(const auto & secondHex : second->getHexes())
				if(secondHex.isValid() && BattleHex::getDistance(firstHex, secondHex) == 1)
					return true;
		}
		return false;
	};
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if(auto state = getHeroOrderState(side, HeroCommand::HOLD_THE_LINE);
			state && !state->containsHoldBroken(id))
		{
			const auto * anchor = state->anchorFor(id);
			if(anchor && anchor->position != destination.toInt())
			{
				state->holdBrokenUnitIds.insert(std::lower_bound(
					state->holdBrokenUnitIds.begin(), state->holdBrokenUnitIds.end(), id), id);
				setHeroOrderState(side, state);
			}
		}

		if(auto state = getHeroOrderState(side, HeroCommand::PROTECT); state && !state->protectBroken)
		{
			const auto * protector = battleGetUnitByID(state->primaryTargetUnitId);
			const auto * ward = battleGetUnitByID(state->secondaryTargetUnitId);
			if(!protector || !ward || !protector->alive() || !ward->alive()
				|| !unitsAdjacent(protector, ward))
			{
				state->protectBroken = true;
				setHeroOrderState(side, state);
			}
		}
	}
}

void HypotheticBattle::updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta)
{
	std::shared_ptr<StackWithBonuses> changed = getForUpdate(id);
	const bool wasAlive = changed->alive();

	changed->load(data);
	recordBloodrageTransition(changed, wasAlive);

	if(healthDelta < 0)
	{
		changed->removeUnitBonus(Bonus::UntilBeingAttacked);
	}
}

void HypotheticBattle::removeUnit(uint32_t id)
{
	std::set<uint32_t> ids;
	ids.insert(id);

	while(!ids.empty())
	{
		auto toRemoveId = *ids.begin();

		auto toRemove = getForUpdate(toRemoveId);
		const bool wasAlive = toRemove->alive();

		if(!toRemove->ghost)
		{
			toRemove->onRemoved();

			//TODO: emulate detachFromAll() somehow

			//stack may be removed instantly (not being killed first)
			//handle clone remove also here
			if(toRemove->cloneID >= 0)
			{
				ids.insert(toRemove->cloneID);
				toRemove->cloneID = -1;
			}

			// Match BattleInfo::removeUnit: deleting a clone releases its original
			// for another Clone cast. Change only this model, including retained
			// dead/ghost descriptors and originals inherited from a parent model.
			const auto remaining = getUnitsIf([](const battle::Unit *) { return true; });
			for(const auto * unit : remaining)
			{
				auto linked = getForUpdate(unit->unitId());
				if(linked->cloneID >= 0 && static_cast<uint32_t>(linked->cloneID) == toRemoveId)
					linked->cloneID = -1;
			}
		}
		recordBloodrageTransition(toRemove, wasAlive);

		ids.erase(toRemoveId);
	}
}

void HypotheticBattle::recordBloodrageTransition(const std::shared_ptr<StackWithBonuses> & unit, bool wasAlive)
{
	if(bloodrageRanks[BattleSide::ATTACKER] == 0 && bloodrageRanks[BattleSide::DEFENDER] == 0)
		return;
	if(unit->alive())
	{
		bloodrageDestroyedUnits.erase(unit->unitId());
		return;
	}
	if(!wasAlive || unit->summoned || unit->isClone() || !bloodrageDestroyedUnits.insert(unit->unitId()).second)
		return;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		bloodrageDamagePercents[side] = std::min(bloodrageCaps[side],
			bloodrageDamagePercents[side] + newHorizonsBloodrage::incrementForRank(bloodrageRanks[side]));
}

void HypotheticBattle::addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	getForUpdate(id)->addUnitBonus(bonus);
	bonusTreeVersion++;
}

void HypotheticBattle::updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	getForUpdate(id)->updateUnitBonus(bonus);
	bonusTreeVersion++;
}

void HypotheticBattle::removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	getForUpdate(id)->removeUnitBonus(bonus);
	bonusTreeVersion++;
}

EWallState HypotheticBattle::getWallState(EWallPart part) const
{
	return projectedWalls.at(part);
}

int32_t HypotheticBattle::getWallStructuralHP(EWallPart part) const
{
	if(!canonicalStructuralHP)
		return 0;
	if(const auto it = projectedStructuralHP.find(part); it != projectedStructuralHP.end())
		return it->second;
	return 0;
}

EGateState HypotheticBattle::getGateState() const
{
	// The authoritative BattleProcessor derives this after catapult/earthquake
	// damage. A destroyed gate must not remain closed in the projected pathfinder.
	return getWallState(EWallPart::GATE) == EWallState::DESTROYED
		? EGateState::DESTROYED : initialGateState;
}

void HypotheticBattle::setWallState(EWallPart partOfWall, EWallState state)
{
	wallChanges |= projectedWalls[partOfWall] != state;
	projectedWalls[partOfWall] = state;
	if(canonicalStructuralHP && state == EWallState::DESTROYED)
		projectedStructuralHP[partOfWall] = 0;
}

void HypotheticBattle::setWallStructuralHP(EWallPart partOfWall, int32_t hp)
{
	if(!canonicalStructuralHP)
		return;

	const auto maximum = SiegeInfo::maximumStructuralHP(partOfWall);
	if(maximum <= 0)
		return;

	const auto bounded = std::clamp(hp, 0, maximum);
	wallChanges |= getWallStructuralHP(partOfWall) != bounded;
	projectedStructuralHP[partOfWall] = bounded;
	setWallState(partOfWall, SiegeInfo::stateFromStructuralHP(partOfWall, bounded));
}

void HypotheticBattle::addObstacle(const ObstacleChanges & changes)
{
	auto obstacle = std::make_shared<SpellCreatedObstacle>();
	obstacle->fromInfo(changes);
	projectedObstacles.push_back(obstacle);
	obstacleChanges = true;
}

void HypotheticBattle::updateObstacle(const ObstacleChanges& changes)
{
	auto changed = std::make_shared<SpellCreatedObstacle>();
	changed->fromInfo(changes);
	for(auto & obstacle : projectedObstacles)
	{
		if(obstacle->uniqueID != changes.id)
			continue;
		const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
		assert(spell);
		if(!spell)
			return;
		auto replacement = std::make_shared<SpellCreatedObstacle>(*spell);
		// Match BattleInfo: UPDATE currently changes revealed, not geometry/TTL.
		replacement->revealed = changed->revealed;
		obstacleChanges |= replacement->revealed != spell->revealed;
		obstacle = replacement;
		break;
	}
}

void HypotheticBattle::removeObstacle(uint32_t id)
{
	const auto found = std::find_if(projectedObstacles.begin(), projectedObstacles.end(),
		[id](const auto & obstacle) { return obstacle->uniqueID == id; });
	if(found != projectedObstacles.end())
	{
		projectedObstacles.erase(found);
		obstacleChanges = true;
	}
}

uint32_t HypotheticBattle::nextUnitId() const
{
	// Parent models can already own IDs in the projected allocation range.
	// Retained ghosts also reserve their identities (clone links/target cohorts).
	const auto maximum = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	while(nextId <= maximum && battleGetUnitByID(nextId))
		++nextId;
	if(nextId > maximum)
		throw std::overflow_error("Hypothetical battle unit identities exhausted");
	return nextId++;
}

int64_t HypotheticBattle::getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const
{
	return (damage.min + damage.max) / 2;
}

std::vector<SpellID> HypotheticBattle::getUsedSpells(BattleSide side) const
{
	// TODO
	return {};
}

int3 HypotheticBattle::getLocation() const
{
	// TODO
	return int3(-1, -1, -1);
}

BattleLayout HypotheticBattle::getLayout() const
{
	return subject->getBattle()->getLayout();
}

int32_t HypotheticBattle::getTreeVersion() const
{
	return getBonusBearer()->getTreeVersion() + bonusTreeVersion;
}

void HypotheticBattle::projectRangedMarkStrike(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits)
{
	if(!attack.shooting || !attack.attacker || !attack.defender || hits.empty()
		|| !newHorizonsMagic::rulesActive(getMagicRules()))
		return;

	const auto attacker = getForUpdate(attack.attacker->unitId());
	CombatEventPayload payload;
	payload.ranged = true;
	payload.isCounter = attack.retaliation;
	for(const auto & [unitId, damage] : hits)
	{
		if(damage <= 0)
			continue;
		const auto * target = battleGetUnitByID(unitId);
		if(!target)
			continue;
		AttackedTarget hit;
		hit.unit = target;
		hit.damage = damage;
		payload.targets.push_back(hit);
	}
	if(payload.targets.empty())
		return;

	const auto triggers = attacker->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
	{
		if(bonus->source != BonusSource::SPELL_EFFECT || !bonus->parameters
			|| bonus->sid.toString() != newHorizonsSorcery::FOCUS_MAGIC_SPELL)
			continue;
		const auto scriptId = bonus->subtype.as<ScriptID>();
		const auto & script = LIBRARY->scriptTypes()->getById(scriptId);
		if(script.scriptId != newHorizonsSorcery::FOCUS_MAGIC_TRIGGER || !script.combatEventScript)
			continue;
		JsonNode parameters = bonus->parameters->toCustom<JsonNode>();
		parameters["val"].Integer() = bonus->val;
		script.combatEventScript->run(getServerCallback(), *this, CombatEventType::AFTER_ATTACK,
			attacker.get(), battleGetUnitByID(attack.defender->unitId()), parameters, payload);
	}
}

int64_t HypotheticBattle::projectHexOfPainStrike(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits, int32_t attackIndex)
{
	if(!attack.attacker || hits.empty())
		return 0;

	const auto attacker = getForUpdate(attack.attacker->unitId());
	const auto healthBefore = attacker->getAvailableHealth();
	CombatEventPayload payload;
	payload.ranged = attack.shooting;
	payload.isCounter = attack.retaliation;
	payload.attackIndex = attackIndex;
	for(const auto & [unitId, damage] : hits)
	{
		const auto * target = battleGetUnitByID(unitId);
		if(!target)
			continue;

		AttackedTarget hit;
		hit.unit = target;
		hit.damage = std::max<int64_t>(0, damage);
		// `hits` already contains post-cap damage from the projected attack. Keep
		// the script's health cap meaningful without needing a second health
		// snapshot at this post-attack callback boundary.
		hit.healthBeforeAttack = hit.damage;
		payload.targets.push_back(hit);
	}
	if(payload.targets.empty())
		return 0;

	const auto triggers = attacker->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
	{
		if(!bonus || !isHexOfPainBonus(*bonus))
			continue;

		const auto scriptId = bonus->subtype.as<ScriptID>();
		const auto & script = LIBRARY->scriptTypes()->getById(scriptId);
		if(!script.combatEventScript)
			continue;

		JsonNode parameters = bonus->parameters->toCustom<JsonNode>();
		parameters["val"].Integer() = bonus->val;
		script.combatEventScript->run(getServerCallback(), *this, CombatEventType::AFTER_ATTACK,
			attacker.get(), attack.defender ? battleGetUnitByID(attack.defender->unitId()) : nullptr,
			parameters, payload);
	}

	return std::max<int64_t>(0, healthBefore - attacker->getAvailableHealth());
}

ServerCallback * HypotheticBattle::getServerCallback()
{
	return serverCallback.get();
}

const scripting::Pool & HypotheticBattle::getScriptContextPool() const
{
	return subject->getBattle()->getScriptContextPool();
}

void HypotheticBattle::makeWait(const battle::Unit * activeStack)
{
	auto unit = getForUpdate(activeStack->unitId());

	resetActiveUnit();
	unit->afterWait();
	unit->setActivationMovementBonus(newHorizonsBattlecraft::delayedActivationMovementBonus(
		battleGetOwnerHero(unit.get()), unit.get(), BattleUnitTurnReason::TURN_QUEUE));
}

HypotheticBattle::HypotheticServerCallback::HypotheticServerCallback(HypotheticBattle * owner_)
	:owner(owner_)
{
}

void HypotheticBattle::HypotheticServerCallback::complain(const std::string & problem)
{
	logAi->error(problem);
}

bool HypotheticBattle::HypotheticServerCallback::describeChanges() const
{
	return false;
}

void HypotheticBattle::HypotheticServerCallback::recordCompletedHeroSpellCast(BattleSide side)
{
	recordCompletedHeroSpellCast(side, 0);
}

void HypotheticBattle::HypotheticServerCallback::recordCompletedHeroSpellCast(BattleSide side, int32_t spellLevel)
{
	if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
	{
		owner->heroSpellCastCompletedStates.at(side) = true;
		if(spellLevel >= 1 && spellLevel <= GameConstants::SPELL_LEVELS)
			owner->completedHeroSpellLevelMasks.at(side) |= static_cast<std::uint8_t>(1u << (spellLevel - 1));
	}
}

vstd::RNG * HypotheticBattle::HypotheticServerCallback::getRNG()
{
	return &rngStub;
}

bool HypotheticBattle::HypotheticServerCallback::rollCombatAbility(const IBattleInfoCallback &, const battle::Unit &, int percentageChance)
{
	// same averaging the RNG stub does - the hypothetical battle must stay deterministic
	return percentageChance >= 50;
}

bool HypotheticBattle::HypotheticServerCallback::rollHostileCombatAbility(const IBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & recipient, int percentageChance)
{
	const int basisPoints = actor.favorableCreatureAbilityChanceBasisPoints(percentageChance);
	int chance = basisPoints / 100;
	if(basisPoints % 100 && rngStub.nextInt(0, 99) < basisPoints % 100)
		++chance;
	const auto draw = [&]() { return rollCombatAbility(battle, actor, chance); };
	const auto actorSide = owner->playerToSide(owner->battleGetOwner(&actor));
	const auto recipientSide = owner->playerToSide(owner->battleGetOwner(&recipient));
	const bool hostile = (actorSide == BattleSide::ATTACKER || actorSide == BattleSide::DEFENDER)
		&& (recipientSide == BattleSide::ATTACKER || recipientSide == BattleSide::DEFENDER)
		&& actorSide != recipientSide && actor.alive() && recipient.alive()
		&& recipient.isValidTarget(false) && !recipient.isInvincible();
	if(!hostile)
		return draw();
	return resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), recipientSide,
		chance > 0 && chance < 100, true, draw);
}

bool HypotheticBattle::HypotheticServerCallback::resolveAdverseCombatRoll(const BattleID & battleID,
	BattleSide side, bool stochastic, bool adverseOnTrue, const std::function<bool()> & draw)
{
	if(battleID != owner->getBattleID())
		throw std::runtime_error("Adverse combat projection refers to another battle");
	const bool firstResult = draw();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return firstResult;
	auto state = owner->getAdverseCombatRerollState(side);
	if(!state.consume(stochastic, firstResult == adverseOnTrue))
		return firstResult;

	BattleAdverseRerollStateChanged packet;
	packet.battleID = battleID;
	packet.side = side;
	packet.state = state;
	BattleStatePackVisitor visitor(*owner);
	packet.visit(visitor);
	return draw();
}

void HypotheticBattle::HypotheticServerCallback::apply(CPackForClient & pack)
{
	logAi->error("Package of type %s is not allowed in battle evaluation", typeid(pack).name());
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleLogMessage & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleStackMoved & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleUnitsChanged & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(SetStackEffect & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(StacksInjured & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleObstaclesChanged & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(CatapultAttack & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

HypotheticBattle::HypotheticEnvironment::HypotheticEnvironment(HypotheticBattle * owner_, const Environment * upperEnvironment)
	: owner(owner_),
	env(upperEnvironment)
{

}

const Services * HypotheticBattle::HypotheticEnvironment::services() const
{
	return env->services();
}

const Environment::BattleCb * HypotheticBattle::HypotheticEnvironment::battle(const BattleID & battleID) const
{
	assert(battleID == owner->getBattleID());
	return owner;
}

const Environment::GameCb * HypotheticBattle::HypotheticEnvironment::game() const
{
	return env->game();
}
