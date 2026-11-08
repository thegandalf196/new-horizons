/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleSpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsRunicReflection.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsNaturesWrath.h"
#include "NewHorizonsPandemonium.h"
#include "../combatScripts/nativeEvents/NativeEventCommon.h"
#include "Problem.h"
#include "RunicCreatureCaster.h"
#include "../ScopeGuard.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../CStack.h"
#include <typeinfo>
#include "../networkPacks/PacksForClientBattle.h"
#include <algorithm>
#include <map>
#include <vstd/RNG.h>

namespace spells
{
BattleSpellMechanics::RunicCasterLifetime::RunicCasterLifetime(BattleSpellMechanics & owner)
	: mechanics(owner), original(owner.caster), originalUnit(owner.runicCreatureCaster
		? owner.battle()->battleGetUnitByID(owner.runicCreatureCaster->getCasterUnitId()) : nullptr),
	  paysCasts(owner.runicCreatureCaster && typeid(*owner.caster) == typeid(CStack))
{
	if(mechanics.runicCreatureCaster) mechanics.caster = mechanics.runicCreatureCaster.get();
}

BattleSpellMechanics::RunicCasterLifetime::~RunicCasterLifetime()
{
	const auto * remaining = mechanics.runicCreatureCaster
		? mechanics.battle()->battleGetUnitByID(mechanics.runicCreatureCaster->getCasterUnitId()) : nullptr;
	if(mechanics.runicCreatureCaster && (remaining != originalUnit || !remaining || remaining->isGhost()))
	{
		mechanics.runicRetiredCaster = true;
		mechanics.eventSnapshot.reset();
		mechanics.caster = mechanics.runicCreatureCaster.get();
	}
	else mechanics.caster = original;
}

void BattleSpellMechanics::RunicCasterLifetime::spendMana(ServerCallback * server, int32_t cost) const
{
	if(!mechanics.runicCreatureCaster) { mechanics.caster->spendMana(server, cost); return; }
	// A registered corpse retains its charge. A removed ghost and native
	// ability/bonus proxies cannot receive an additional CASTS payment.
	if(paysCasts)
		if(const auto * unit = mechanics.battle()->battleGetUnitByID(mechanics.runicCreatureCaster->getCasterUnitId());
			unit && !unit->isGhost()) unit->spendMana(server, cost);
}

BattleSpellMechanics::BattleSpellMechanics(const BattleSpellMechanics & source, RunicReadOnlyView)
	: BaseMechanics(source), runicReadOnly(true), eventSnapshot(source.eventSnapshot),
	  effects(source.effects), targetCondition(source.targetCondition)
{
	if(source.runicOffensiveProfile) runicOffensiveProfile = source.runicOffensiveProfile;
	else if(const auto * profile = source.getMetamagicCastProfile()) runicOffensiveProfile = *profile;
	runicCreatureOutputPercent = source.mode == Mode::MAGIC_MIRROR ? source.runicCreatureOutputPercent : source.getDirectCreatureActivationDamagePercent();
	runicCreatureInspirationPercent = source.mode == Mode::MAGIC_MIRROR ? source.runicCreatureInspirationPercent : source.getInspirationalLeaderCreatureDamagePercent();
	if(source.runicCreatureCaster)
	{
		runicCreatureCaster = copyRunicCreatureCaster(*source.runicCreatureCaster);
		caster = runicCreatureCaster.get();
	}
	mode = Mode::MAGIC_MIRROR;
	applyingRunicShare = true;
}
std::unique_ptr<Mechanics> BattleSpellMechanics::runicReflectionView() const
{
	return std::unique_ptr<Mechanics>(new BattleSpellMechanics(*this, RunicReadOnlyView{}));
}

bool BattleSpellMechanics::isRunicReflection(const battle::Unit * unit) const
{
	if(!unit || unit->magicResistance() >= 100 || mode == Mode::MAGIC_MIRROR || !activeResistanceServer
		|| !activeResistanceRng || !isMagicalEffect() || !isReceptive(unit)
		|| (!isNegativeSpell() && !newHorizonsNaturesWrath::enabled(*this))
		|| battle()->battleGetOwner(unit) == battle()->sideToPlayer(effectiveCasterSide()))
		return false;
	const auto sample = std::ranges::find(resistanceRolls, unit->unitId(), &ResistanceRoll::unitId);
	return sample != resistanceRolls.end() && sample->probability < 100 && sample->resisted
		&& newHorizonsRunicReflection::outcome(sample->probability,
			newHorizonsRunicReflection::nativeChance(*unit), sample->sample, true)
			== newHorizonsRunicReflection::Outcome::Reflect;
}

void BattleSpellMechanics::recordResistanceShare(const effects::Effect * effect,
	const battle::Unit * unit, size_t index) const
{
	if(!effect || !isRunicReflection(unit)) return;
	if(effect->resistanceShareKind() == effects::ResistanceShareKind::Pandemonium
		&& getPandemoniumDebuffCount(unit) == 0) return;
	const auto same = [&](const RunicShare & share)
	{ return share.effect == effect && share.unitId == unit->unitId(); };
	// A canonical chain may encounter a resisted hop before its final target
	// list exists. Keep that receipt rather than renumbering filtered targets.
	if(std::ranges::none_of(pendingRunicShares, same))
		pendingRunicShares.push_back({effect, unit->unitId(), index});
}

void BattleSpellMechanics::recordResistanceDamageShare(const effects::Effect * effect,
	const battle::Unit * unit, int64_t raw) const
{
	if(effect && raw >= 0 && isRunicReflection(unit))
		pendingRunicShares.push_back({effect, unit->unitId(), 0, 0, raw});
}

void BattleSpellMechanics::recordResistanceShares(const effects::Effects::EffectsToApply & prepared) const
{
	for(const auto & [effect, destinations] : prepared)
		for(size_t index = 0; index < destinations.size(); ++index)
			if(const auto * unit = destinations[index].unitValue; unit && wouldResist(unit))
				recordResistanceShare(effect, unit, index);
}

const battle::Unit * BattleSpellMechanics::chooseRunicRecipient(const std::vector<RunicShare> & shares, vstd::RNG & rng, const battle::Unit * reflector) const
{
	std::vector<const effects::Effect *> effects;
	for(const auto & share : shares) effects.push_back(share.effect);
	const auto view = runicReflectionView();
	const auto candidates = newHorizonsRunicReflection::legalRecipients(*view, effects, reflector);
	return candidates.empty() ? nullptr : *RandomGeneratorUtil::nextItem(candidates, rng);
}

void BattleSpellMechanics::prepareRunicReflections(BattleSpellCast & cast, vstd::RNG & rng, bool late)
{
	if(mode == Mode::MAGIC_MIRROR) return;
	if(!caster->getHeroCaster() && !runicCreatureCaster && !admitsRunicCreatureCaster(*caster, *battle()))
	{ pendingRunicShares.clear(); return; }
	const auto originalMode = mode;
	if(!late)
	{
		if(const auto * profile = BaseMechanics::getMetamagicCastProfile()) runicOffensiveProfile = *profile;
		else runicOffensiveProfile.reset();
		runicCreatureOutputPercent = BaseMechanics::getDirectCreatureActivationDamagePercent();
		runicCreatureInspirationPercent = BaseMechanics::getInspirationalLeaderCreatureDamagePercent();
		// Hand of Fate chooses its spill only after primary damage. Protect a
		// native creature caster before those effects can remove its source.
		if(!caster->getHeroCaster() && owner->getJsonKey() == "new-horizons:handOfFate"
			&& std::ranges::any_of(resistanceRolls, [&](const ResistanceRoll & roll)
			{
				const auto * unit = battle()->battleGetUnitByID(roll.unitId);
				return unit && newHorizonsRunicReflection::nativeChance(*unit) > 0;
			}))
			runicCreatureCaster = captureRunicCreatureCaster(*caster, *owner, *battle(), affectedUnits);
	}
	if(pendingRunicShares.empty()) return;
	const auto restore = vstd::makeScopeGuard([&] { mode = originalMode; });
	std::map<uint32_t, std::vector<RunicShare>> groups;
	for(const auto & share : pendingRunicShares) groups[share.unitId].push_back(share);
	for(const auto & [sourceId, shares] : groups)
	{
		mode = originalMode;
		const auto * source = battle()->battleGetUnitByID(sourceId);
		if(!late && (!source || !isRunicReflection(source))) continue;
		// Paired/unknown effects have no legal one-recipient replay contract.
		if(std::ranges::any_of(shares, [](const RunicShare & share)
			{ return share.effect->resistanceShareKind() == effects::ResistanceShareKind::Unsupported; })) continue;
		mode = Mode::MAGIC_MIRROR;
		const auto * recipient = chooseRunicRecipient(shares, rng, source);
		if(!recipient) continue;
		cast.reflectedCres.insert(sourceId);
		cast.resistedCres.erase(sourceId);
		if(late)
		{
			BattleLogMessage feedback; feedback.battleID = battle()->getBattle()->getBattleID();
			MetaString text; text.appendTextID("new-horizons.combat.runicEndurance.reflected");
			feedback.lines.push_back(std::move(text)); activeResistanceServer->apply(feedback);
		}
		const int probability = newHorizonsMetamagic::resistanceChance(std::min(recipient->magicResistance(), 100),
			getMetamagicCastProfile(), *battle(), *recipient);
		const auto draw = [&] { return rng.nextInt(0, 99) < probability; };
		const bool resisted = probability > 0 && probability < 100
			? activeResistanceServer->resolveAdverseCombatRoll(battle()->getBattle()->getBattleID(),
				casterSide, true, false, draw) : draw();
		if(resisted)
		{
			cast.resistedCres.insert(recipient->unitId());
			continue;
		}
		for(const auto & share : shares)
			reflectedRunicShares.push_back({share.effect, recipient->unitId(), share.index,
				share.effect->resistanceShareKind() == effects::ResistanceShareKind::Pandemonium
					? getPandemoniumDebuffCount(recipient) : 0, share.rawDamage, sourceId});
		if(!vstd::contains(affectedUnits, recipient)) affectedUnits.push_back(recipient);
		if(source && std::ranges::any_of(shares, [](const RunicShare & share)
			{ return share.effect->resistanceShareKind() == effects::ResistanceShareKind::LifeDrain; })
			&& !vstd::contains(affectedUnits, source)) affectedUnits.push_back(source);
	}
	pendingRunicShares.clear();
	if(!late && !runicCreatureCaster && !reflectedRunicShares.empty() && !caster->getHeroCaster())
		runicCreatureCaster = captureRunicCreatureCaster(*caster, *owner, *battle(), affectedUnits);
}

void BattleSpellMechanics::applyRunicReflections(ServerCallback * server)
{
	if(mode != Mode::MAGIC_MIRROR) throw std::logic_error("Runic share has no reflected caster context");
	const auto previous = applyingRunicShare;
	applyingRunicShare = true;
	const auto restore = vstd::makeScopeGuard([&] { applyingRunicShare = previous; });
	for(const auto & share : reflectedRunicShares)
	{
		// Original effects may destroy or remove a prospective recipient. Retain
		// IDs in the plan and reacquire; do not keep a borrowed unit across effects.
		const auto * recipient = battle()->battleGetUnitByID(share.unitId);
		if(!recipient || !recipient->isValidTarget(false) || !isReceptive(recipient) || isSpellLocked(recipient)
			|| battle()->battleGetOwner(recipient) != battle()->sideToPlayer(casterSide)) continue;
		const auto kind = share.effect->resistanceShareKind();
		Target currentTarget{Destination(recipient)};
		if(kind == effects::ResistanceShareKind::LifeDrain)
		{
			const auto * source = battle()->battleGetUnitByID(share.reflectorId);
			if(!source || isSpellLocked(source)) continue;
			currentTarget.emplace_back(source);
		}
		detail::ProblemImpl problem;
		if(!share.effect->applicableTarget(problem, this, currentTarget)
			|| share.effect->filterTarget(this, currentTarget).size() != currentTarget.size()) continue;
		if(kind == effects::ResistanceShareKind::LifeDrain)
		{
			share.effect->apply(server, this, currentTarget);
			continue;
		}
		if(kind == effects::ResistanceShareKind::Pandemonium && share.capturedCount == 0) continue;
		if(share.rawDamage >= 0 || kind == effects::ResistanceShareKind::NaturesWrath || kind == effects::ResistanceShareKind::Pandemonium)
		{
			const int64_t amount = share.rawDamage >= 0 ? adjustRecipientDamage(recipient, share.rawDamage)
				: kind == effects::ResistanceShareKind::NaturesWrath
					? getNaturesWrathDamage(recipient, static_cast<int32_t>(share.index))
					: getPandemoniumDamage(recipient, share.capturedCount);
			const nativeEvents::Context context{*server, *battle(), nullptr, nullptr, JsonNode(), {}};
			if(share.rawDamage >= 0 && amount <= 0)
			{
				nativeEvents::clearFrozen(context, *recipient);
				continue;
			}
			const auto [damage, killed] = nativeEvents::damage(context, *recipient, amount, getUnitCaster(), true);
			const auto * current = battle()->battleGetUnitByID(share.unitId);
			if(current) nativeEvents::clearFrozen(context, *current);
			if(damage > 0 && server->describeChanges())
				nativeEvents::spellDamageLog(context, *getSpellId().toSpell(), current, damage, killed);
			continue;
		}
		Target destination(share.index + 1);
		destination.back() = Destination(recipient);
		share.effect->apply(server, this, destination);
	}
}
}
