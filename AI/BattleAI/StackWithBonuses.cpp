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
#include "../../lib/battle/NewHorizonsBloodrage.h"
#include "../../lib/battle/SiegeInfo.h"

#include <vcmi/events/EventBus.h>

#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/CStack.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"

namespace
{
bool projectedEffect(const Bonus * bonus)
{
	return bonus->source == BonusSource::SPELL_EFFECT || bonus->source == BonusSource::HERO_COMMAND;
}

bool timedProjectionEffect(const Bonus * bonus)
{
	return Bonus::NTurns(bonus) && projectedEffect(bonus);
}
}

void actualizeEffect(TBonusListPtr target, const Bonus & ef)
{
	for(auto & bonus : *target) //TODO: optimize
	{
		if(bonus->source == ef.source && bonus->sid == ef.sid && bonus->type == ef.type
			&& bonus->subtype == ef.subtype && bonus->valType == ef.valType)
		{
			if(bonus->turnsRemain < ef.turnsRemain)
			{
				bonus.reset(new Bonus(*bonus));

				bonus->turnsRemain = ef.turnsRemain;
			}
		}
	}
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::CUnitState * Stack)
	: battle::CUnitState(),
	origBearer(Stack->getBonusBearer()),
	owner(Owner),
	type(Stack->unitType()),
	baseAmount(Stack->unitBaseAmount()),
	id(Stack->unitId()),
	side(Stack->unitSide()),
	player(Stack->unitOwner()),
	slot(Stack->unitSlot()),
	treeVersionLocal(0)
{
	localInit(Owner);

	battle::CUnitState::operator=(*Stack);
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::Unit * Stack)
	: battle::CUnitState(),
	origBearer(Stack->getBonusBearer()),
	owner(Owner),
	type(Stack->unitType()),
	baseAmount(Stack->unitBaseAmount()),
	id(Stack->unitId()),
	side(Stack->unitSide()),
	player(Stack->unitOwner()),
	slot(Stack->unitSlot()),
	treeVersionLocal(0)
{
	localInit(Owner);

	auto state = Stack->acquireState();
	battle::CUnitState::operator=(*state);
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::UnitInfo & info)
	: battle::CUnitState(),
	origBearer(nullptr),
	owner(Owner),
	baseAmount(info.count),
	id(info.id),
	side(info.side),
	slot(SlotID::SUMMONED_SLOT_PLACEHOLDER),
	treeVersionLocal(0)
{
	type = info.type.toCreature();
	origBearer = type;

	player = Owner->getSidePlayer(side);

	localInit(Owner);

	position = info.position;
	summoned = info.summoned;
}

StackWithBonuses::~StackWithBonuses() = default;

StackWithBonuses & StackWithBonuses::operator=(const battle::CUnitState & other)
{
	battle::CUnitState::operator=(other);
	return *this;
}

const CCreature * StackWithBonuses::unitType() const
{
	return type;
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
	auto ret = std::make_shared<BonusList>();
	// Refresh changes duration, not value. Filtering before merging can hide the
	// existing identity and incorrectly turn a refresh into a new effect.
	const auto & mergeSelector = bonusesToUpdate.empty() ? selector : Selector::all;
	TConstBonusListPtr originalList = origBearer->getAllBonuses(mergeSelector,
		bonusesToUpdate.empty() ? cachingStr : std::string());

	vstd::copy_if(*originalList, std::back_inserter(*ret), [this](const std::shared_ptr<Bonus> & b)
	{
		return !vstd::contains(bonusesToRemove, b)
			&& !(projectedEffects && projectedEffect(b.get()));
	});

	if(projectedEffects)
		for(const auto & bonus : *projectedEffects)
			if(mergeSelector(&bonus))
				ret->push_back(std::make_shared<Bonus>(bonus));

	for(const Bonus & bonus : bonusesToUpdate)
	{
		if(mergeSelector(&bonus))
		{
			if(ret->getFirst(Selector::source(BonusSource::SPELL_EFFECT, bonus.sid).And(Selector::typeSubtypeValueType(bonus.type, bonus.subtype, bonus.valType))))
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
	if(!bonusesToUpdate.empty())
		ret->remove_if([&](const Bonus * bonus){ return !selector(bonus); });
	//TODO limiters?
	return ret;
}

int32_t StackWithBonuses::getTreeVersion() const
{
	auto result = owner->getTreeVersion();

	if(bonusesToAdd.empty() && bonusesToUpdate.empty() && bonusesToRemove.empty() && !projectedEffects)
		return result;
	else
		return result + treeVersionLocal;
}

void StackWithBonuses::addUnitBonus(const std::vector<Bonus> & bonus)
{
	vstd::concatenate(bonusesToAdd, bonus);
	treeVersionLocal++;
}

void StackWithBonuses::updateUnitBonus(const std::vector<Bonus> & bonus)
{
	// Preserve operation order: a preceding local ADD must be visible to refresh.
	captureEffects();
	vstd::concatenate(bonusesToUpdate, bonus);
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

void StackWithBonuses::removeUnitBonus(const CSelector & selector)
{
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

	treeVersionLocal++;
}

void StackWithBonuses::captureEffects()
{
	// Resolve refreshes before aging or another mutation. Their retained values
	// and refreshed durations must survive together, not as independent vectors.
	const auto effects = getAllBonuses(CSelector(projectedEffect));
	projectedEffects.emplace();
	for(const auto & bonus : *effects)
		projectedEffects->push_back(*bonus);
	vstd::erase_if(bonusesToAdd, [](const Bonus & bonus){ return projectedEffect(&bonus); });
	vstd::erase_if(bonusesToUpdate, [](const Bonus & bonus){ return projectedEffect(&bonus); });
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
	const auto age = [](std::vector<Bonus> & bonuses)
	{
		for(auto & bonus : bonuses)
			if(timedProjectionEffect(&bonus) && bonus.turnsRemain > 0)
				--bonus.turnsRemain;
		vstd::erase_if(bonuses, [](const Bonus & bonus)
		{
			return timedProjectionEffect(&bonus) && bonus.turnsRemain <= 0;
		});
	};
	age(*projectedEffects);
	age(bonusesToAdd);
	age(bonusesToUpdate);
	++treeVersionLocal;
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
	fortuneRollRules = realBattle->getBattle()->getLuckRollRules();
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
		heroOrderStates[side] = realBattle->getBattle()->getHeroOrderState(side);
		focusFireStates[side] = realBattle->battleGetFocusFireState(side);
		fortuneStates[side] = realBattle->getBattle()->getSylvanLuckState(side);
		bloodrageRanks[side] = realBattle->getBattle()->getBloodrageRank(side);
		bloodrageDamagePercents[side] = realBattle->getBattle()->getBloodrageDamagePercent(side);
	}

	localEnvironment.reset(new HypotheticEnvironment(this, env));
	serverCallback.reset(new HypotheticServerCallback(this));
}

bool HypotheticBattle::unitHasAmmoCart(const battle::Unit * unit) const
{
	return battleUnitHasAmmoCart(unit);
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

void HypotheticBattle::projectFortuneStrike(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits,
	battle::CUnitState * attackerState, bool enemyStackKilled)
{
	const auto side = playerToSide(battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return;

	auto & fortune = fortuneStates.at(side);
	if(!fortune.active())
		return;

	const int luck = battleGetAttackLuck(attack.attacker, attack.defender, attack.shooting);
	const auto rules = getLuckRollRules();
	const bool positive = attack.luckyStrike || (luck > 0 && fortuneStrikeIsCertain(attack));
	const bool negative = attack.unluckyStrike || (luck < 0 && fortuneStrikeIsCertain(attack));
	if(!positive && !negative)
		return;

	// Negative Providence history is committed as well, but it has no
	// aftermath to project.  recordStrike returns true when that bad result was
	// suppressed by Providence, exactly as it does in the authoritative path.
	if(fortune.recordStrike(attack.attacker->unitId(), positive, negative))
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

std::optional<HeroOrderState> HypotheticBattle::getHeroOrderState(BattleSide side) const
{
	return heroOrderStates.at(side);
}

void HypotheticBattle::setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Hero Order side");
	if(state)
		state->validateShape();
	heroOrderStates.at(side) = state;
	++bonusTreeVersion;
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

int32_t HypotheticBattle::getRound() const
{
	return projectedRound;
}

int32_t HypotheticBattle::getBloodrageDamagePercent(BattleSide side) const
{
	return bloodrageDamagePercents.at(side);
}

IBattleInfo::ObstacleCList HypotheticBattle::getAllObstacles() const
{
	return projectedObstacles;
}

void HypotheticBattle::nextRound()
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		fortuneStates[side].nextRound();
	for(auto & [side, state] : focusFireStates)
		state.reset();
	++bonusTreeVersion;
	// BattleInfo grants opening effects their full duration in round one.
	const bool firstRound = projectedRound == 0;
	++projectedRound;
	std::vector<uint32_t> pendingRemoval;
	for(const auto * unit : getUnitsIf([](const battle::Unit *) { return true; }))
	{
		auto forUpdate = getForUpdate(unit->unitId());
		if(!firstRound && !forUpdate->isTimeStopped())
			forUpdate->advanceTimedRound();
		forUpdate->afterNewRound();
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
	if(reason == BattleUnitTurnReason::ACTION_REJECTED)
		return;
	if(battleBeginsActivation(unit.get(), reason))
	{
		const auto side = playerToSide(battleGetOwner(unit.get()));
		for(auto owner : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			fortuneStates.at(owner).beginActivation(unitId, side == owner);
	}

	if(!unit->isTimeStopped() && reason != BattleUnitTurnReason::UNIT_SPELLCAST && reason != BattleUnitTurnReason::HERO_COMMAND)
		unit->removeUnitBonus(Bonus::UntilGetsTurn);

	unit->afterGetsTurn(reason);
}

void HypotheticBattle::addUnit(uint32_t id, const JsonNode & data)
{
	battle::UnitInfo info;
	info.load(id, data);
	auto newUnit = std::make_shared<StackWithBonuses>(this, info);
	stackStates[newUnit->unitId()] = newUnit;
}

void HypotheticBattle::moveUnit(uint32_t id, const BattleHex & destination)
{
	std::shared_ptr<StackWithBonuses> changed = getForUpdate(id);
	changed->position = destination;
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
		bloodrageDamagePercents[side] = std::min(newHorizonsBloodrage::capForRank(bloodrageRanks[side]),
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

vstd::RNG * HypotheticBattle::HypotheticServerCallback::getRNG()
{
	return &rngStub;
}

bool HypotheticBattle::HypotheticServerCallback::rollCombatAbility(const IBattleInfoCallback &, const battle::Unit &, int percentageChance)
{
	// same averaging the RNG stub does - the hypothetical battle must stay deterministic
	return percentageChance >= 50;
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
