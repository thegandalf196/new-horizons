/*
 * LiveUnitEnvironmentContext.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "LiveUnitEnvironmentContext.h"
#include "CBattleInfoCallback.h"
#include "BattleInfo.h"
#include "CommittedElementalEffects.h"
#include "CommittedBonusOperations.h"
#include "NewHorizonsDiscipline.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/artifact/CArtifactInstance.h"
#include "../entities/artifact/CArtifact.h"

bool battle::heroHasAmmoCartArtifact(const CGHeroInstance * hero)
{
	if(!hero)
		return false;
	const auto artifact = hero->artifactsWorn.find(ArtifactPosition::MACH2);
	return artifact != hero->artifactsWorn.end()
		&& artifact->second.getArt()->getTypeId() == ArtifactID::AMMO_CART;
}

battle::LiveUnitEnvironmentContext::LiveUnitEnvironmentContext(const CBattleInfoCallback & callback)
	: callback(callback), owners(callback) {}
bool battle::LiveUnitEnvironmentContext::available() const { return callback.getBattle() != nullptr; }
const battle::UnitOwnershipQuery & battle::LiveUnitEnvironmentContext::ownership() const { return owners; }
battle::Units battle::LiveUnitEnvironmentContext::unitsIf(const UnitFilter & predicate) const { return callback.battleGetUnitsIf(predicate); }
bool battle::LiveUnitEnvironmentContext::sideHasAmmoArtifact(BattleSide side) const
{
	return heroHasAmmoCartArtifact(callback.getBattle()->getSideHero(side));
}
bool battle::LiveUnitEnvironmentContext::sideHasVeteranCohesion(BattleSide side) const
{
	return newHorizonsDiscipline::hasVeteranCohesion(callback.battleGetFightingHero(side));
}
SylvanLuckState battle::LiveUnitEnvironmentContext::fortune(BattleSide side) const { return callback.getBattle()->getSylvanLuckState(side); }
int battle::LiveUnitEnvironmentContext::bloodrageDamage(BattleSide side) const { return callback.getBattle()->getBloodrageDamagePercent(side); }
int battle::LiveUnitEnvironmentContext::bloodrageRank(BattleSide side) const { return callback.getBattle()->getBloodrageRank(side); }
int battle::LiveUnitEnvironmentContext::bloodrageCap(BattleSide side) const { return callback.getBattle()->getBloodrageCapPercent(side); }
int battle::LiveUnitEnvironmentContext::bloodrageSpeed(BattleSide side) const { return callback.getBattle()->getBloodrageSpeedBonus(side); }
int battle::LiveUnitEnvironmentContext::bloodrageRetaliations(BattleSide side) const { return callback.getBattle()->getBloodrageAdditionalRetaliations(side); }
int battle::LiveUnitEnvironmentContext::bloodragePain(BattleSide side) const { return callback.getBattle()->getBloodragePainIncrement(side); }
int battle::LiveUnitEnvironmentContext::magicResistance(const Unit * unit) const { return callback.battleGetMagicResistance(unit); }
const JsonNode & battle::LiveUnitEnvironmentContext::magicRules() const { return callback.getBattle()->getMagicRules(); }

bool CBattleInfoCallback::battleUnitHasAmmoCart(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitHasAmmoCart(unit);
}
int CBattleInfoCallback::battleSylvanTacticsSpeed(const battle::Unit * unit, int turn) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitSylvanTacticsSpeed(unit, turn);
}
int CBattleInfoCallback::battleSylvanTacticsInitiativePercent(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitSylvanTacticsInitiativePercent(unit);
}
int CBattleInfoCallback::battleFortuneSpeed(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitFortuneSpeed(unit);
}
int CBattleInfoCallback::battleBloodrageSpeed(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitSpeedBonus(unit);
}
int CBattleInfoCallback::battleBloodrageRetaliations(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitAdditionalRetaliations(unit);
}
int CBattleInfoCallback::battleBloodragePainIncrement(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).unitBloodragePainIncrement(unit);
}
int CBattleInfoCallback::battleGetBloodrageDamagePercent(const battle::Unit * unit) const
{
	const battle::LiveUnitEnvironmentContext context(*this);
	return battle::UnitEnvironmentQueries(context).bloodrageDamagePercent(unit);
}
bool CStack::unitHasVeteranCohesion(const battle::Unit * unit) const
{
	if(!battle || !unit)
		return false;
	const battle::LiveUnitEnvironmentContext context(*battle);
	return battle::UnitEnvironmentQueries(context).unitHasVeteranCohesion(unit);
}
bool CStack::unitSlowAffectsSpeed(const battle::Unit * unit) const
{
	if(!battle || !unit)
		return false;
	const battle::LiveUnitEnvironmentContext context(*battle);
	return battle::UnitEnvironmentQueries(context).unitSlowAffectsSpeed(unit);
}

std::optional<std::pair<int32_t, int32_t>> CStack::unitMoraleLimits(const battle::Unit * unit) const
{
	if(!battle || !unit)
		return std::nullopt;
	const battle::LiveUnitEnvironmentContext context(*battle);
	return battle::UnitEnvironmentQueries(context).unitMoraleLimits(unit);
}

newHorizonsNecromancy::CombatPassives battle::LiveUnitEnvironmentContext::necromancyPassives(BattleSide side) const
{
	const auto observer = callback.battleGetMySide();
	const bool known = observer == BattleSide::ALL_KNOWING || observer == side;
	return newHorizonsNecromancy::captureCombatPassives(known ? callback.battleGetFightingHero(side) : nullptr);
}

newHorizonsElementalAttunement::Profile battle::LiveUnitEnvironmentContext::elementalAttunementProfile(BattleSide side) const
{
	if(!knowsElementalAttunementProfile(side))
		return {};
	auto profile = callback.getBattle()->getElementalAttunementProfile(side);
	if(profile.stoneboundMagic)
		if(const auto * hero = callback.battleGetFightingHero(side))
			profile.stoneboundBasisPoints = newHorizonsElementalAttunement::stoneboundReductionBasisPoints(
				hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER));
	return profile;
}
uint8_t battle::LiveUnitEnvironmentContext::elementalAttunementMask(BattleSide side) const
{
	return callback.getBattle()->getElementalAttunementMask(side);
}

bool battle::LiveUnitEnvironmentContext::knowsElementalAttunementProfile(BattleSide side) const
{
	const auto observer = callback.battleGetMySide();
	return observer == BattleSide::ALL_KNOWING || observer == side;
}

std::optional<newHorizonsElementalAttunement::UnitPassives>
battle::LiveUnitEnvironmentContext::committedElementalAttunement(const Unit * unit) const
{
	if(!unit || !available()) return std::nullopt;
	const auto side = unit->unitSide();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return std::nullopt;
	if(knowsElementalAttunementProfile(side)) return std::nullopt;
	// Public effective HP, attacks, mitigation and retaliation are already unit
	// observations. Only a real registered native occurrence may supply them;
	// an ID-compatible detached/foreign provider is not another observation.
	const auto * native = dynamic_cast<const BattleInfo *>(callback.getBattle());
	const auto * stack = dynamic_cast<const CStack *>(unit);
	if(!native || !stack || stack->getBattle() != native
		|| native->battleGetStackByID(unit->unitId(), false) != unit)
		throw std::invalid_argument("Committed elemental facts require the registered native unit occurrence");
	auto profile = callback.getBattle()->getElementalAttunementProfile(side);
	if(profile.stoneboundMagic)
		if(const auto * hero = callback.getBattle()->getSideHero(side))
			profile.stoneboundBasisPoints = newHorizonsElementalAttunement::stoneboundReductionBasisPoints(
				hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER));
	return newHorizonsElementalAttunement::passivesForCreature(profile,
		callback.getBattle()->getElementalAttunementMask(side), unit->creatureId());
}

std::shared_ptr<const battle::CommittedElementalEffects>
battle::LiveUnitEnvironmentContext::committedElementalEffects() const
{
	const auto * native = dynamic_cast<const BattleInfo *>(callback.getBattle());
	if(!native) return {};
	// Exact native registration is checked before creating the opaque table;
	// callers cannot capture hidden army effects from an unsealed mock subject.
	for(const auto * unit : unitsIf([](const Unit *) { return true; }))
	{
		const auto * stack = dynamic_cast<const CStack *>(unit);
		if(!stack || stack->getBattle() != native || native->battleGetStackByID(unit->unitId(), false) != unit)
			throw std::invalid_argument("Elemental transition capture requires registered native occurrences");
	}
	return std::shared_ptr<const CommittedElementalEffects>(new CommittedElementalEffects(callback));
}

std::shared_ptr<const battle::CommittedBonusOperations>
battle::LiveUnitEnvironmentContext::committedBonusOperations() const
{
	if(!dynamic_cast<const BattleInfo *>(callback.getBattle())) return {};
	auto result = std::shared_ptr<const CommittedBonusOperations>(new CommittedBonusOperations(callback));
	return result->empty() ? nullptr : result;
}
