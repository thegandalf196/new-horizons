/*
 * NewHorizonsSoulChain.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "NewHorizonsSoulChain.h"

#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../CStack.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusParameters.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSpellAvailability.h"
#include "../networkPacks/PacksForClientBattle.h"

namespace
{
SpellID soulChainSpellId()
{
	static const SpellID value(SpellID::decode(std::string(newHorizonsSoulChain::SPELL_ID)));
	return value;
}

bool validBattleSide(const BattleSide side)
{
	return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
}

ScriptID soulChainStatusScriptId()
{
	static const ScriptID value(ScriptID::decode("core:soulChainStatus"));
	return value;
}
}

namespace newHorizonsSoulChain
{
bool isEnabled(const JsonNode & savedMagicRules)
{
	const auto spell = soulChainSpellId();
	return spell != SpellID::NONE && newHorizonsMagic::rulesActive(savedMagicRules)
		&& savedMagicRules["rulesetVersion"].Integer()
			>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& newHorizonsMagic::spellAllowedBySavedRoster(savedMagicRules, spell);
}

bool validEnemyTargetSet(const CBattleInfoCallback & battle, const BattleSide casterSide,
	const battle::Target & target)
{
	if(!validBattleSide(casterSide) || target.empty() || target.size() > MAX_TARGETS)
		return false;

	std::set<uint32_t> distinctUnitIds;
	for(const auto & destination : target)
	{
		const auto * unit = destination.unitValue;
		if(!unit || !unit->alive() || !unit->isValidTarget(false) || unit->isInvincible()
			|| unit->unitSide() != battle.otherSide(casterSide)
			|| !distinctUnitIds.insert(unit->unitId()).second)
			return false;
	}
	return true;
}

int32_t echoPercentBasisPoints(const int32_t rawSpellPower,
	const int32_t spellPowerCoefficientBasisPoints, const bool soulBinderActive)
{
	if(rawSpellPower < 0 || spellPowerCoefficientBasisPoints < 0
		|| spellPowerCoefficientBasisPoints > 100'000)
		throw std::invalid_argument("Invalid Soul Chain Spell Power coefficient inputs");

	const int64_t powerTerm = static_cast<int64_t>(rawSpellPower) * 10
		* spellPowerCoefficientBasisPoints / BASIS_POINTS_PER_WHOLE;
	const int64_t baseEcho = std::min<int64_t>(BASE_ECHO_CAP_BASIS_POINTS, 2'000 + powerTerm);
	return static_cast<int32_t>(baseEcho
		+ (soulBinderActive ? SOUL_BINDER_BONUS_BASIS_POINTS : 0));
}

int64_t echoDamage(const int64_t actualSecondaryDamage, const int32_t echoBasisPoints)
{
	if(actualSecondaryDamage <= 0)
		return 0;
	if(echoBasisPoints < 0 || echoBasisPoints > BASE_ECHO_CAP_BASIS_POINTS + SOUL_BINDER_BONUS_BASIS_POINTS)
		throw std::invalid_argument("Soul Chain echo percentage is out of range");

	// Split before multiplication so even a maximum-width health value cannot
	// overflow while applying the bounded 55% maximum.
	return actualSecondaryDamage / BASIS_POINTS_PER_WHOLE * echoBasisPoints
		+ (actualSecondaryDamage % BASIS_POINTS_PER_WHOLE) * echoBasisPoints
			/ BASIS_POINTS_PER_WHOLE;
}

std::optional<Link> linkFor(const battle::Unit * secondary)
{
	if(!secondary)
		return std::nullopt;

	const auto spell = soulChainSpellId();
	if(spell == SpellID::NONE)
		return std::nullopt;

	const auto markerSelector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::typeSubtype(BonusType::COMBAT_EVENT_TRIGGER,
			BonusSubtypeID(soulChainStatusScriptId())));
	const auto markers = secondary->getBonuses(markerSelector);
	for(const auto & marker : *markers)
	{
		if(!marker || !marker->parameters || marker->val < 2'000
			|| marker->val > BASE_ECHO_CAP_BASIS_POINTS + SOUL_BINDER_BONUS_BASIS_POINTS)
			continue;

		try
		{
			const auto parameters = marker->parameters->toCustom<JsonNode>();
			const auto primaryUnitId = parameters["primaryUnitId"];
			const auto casterSide = parameters["casterSide"];
			if(!primaryUnitId.isNumber() || !casterSide.isNumber())
				continue;

			const int64_t primaryValue = primaryUnitId.Integer();
			const int64_t sideValue = casterSide.Integer();
			if(primaryValue < 0 || primaryValue > std::numeric_limits<uint32_t>::max())
				continue;

			const auto side = static_cast<BattleSide>(sideValue);
			if(!validBattleSide(side))
				continue;

			return Link{static_cast<uint32_t>(primaryValue), side, marker->val};
		}
		catch(const std::exception &)
		{
			// A malformed or legacy marker is inert rather than crashing damage.
		}
	}
	return std::nullopt;
}

bool isEchoHit(const BattleStackAttacked & hit)
{
	const auto spell = soulChainSpellId();
	return spell != SpellID::NONE && hit.isSpell() && hit.spellID == spell;
}

int64_t adjustedEchoDamage(const CBattleInfoCallback & battle, const BattleSide casterSide,
	const battle::Unit * primary, const int64_t actualSecondaryDamage, const int32_t echoBasisPoints)
{
	if(!primary || !primary->alive())
		return 0;

	const auto rawDamage = echoDamage(actualSecondaryDamage, echoBasisPoints);
	if(rawDamage <= 0)
		return 0;

	const auto spell = soulChainSpellId();
	const auto * definition = spell != SpellID::NONE ? spell.toSpell() : nullptr;
	if(!definition)
		return 0;

	const spells::Caster * caster = validBattleSide(casterSide)
		? battle.battleGetFightingHero(casterSide) : nullptr;
	if(!caster)
		for(const auto * unit : battle.battleGetAllStacks(true))
			if(unit && unit->unitSide() == casterSide)
			{
				caster = unit;
				break;
			}
	if(!caster)
		return rawDamage;

	return definition->adjustRawDamage(caster, primary, rawDamage, 0,
		battle.battleGetHoldTheLineMagicalReductionBasisPoints(primary), 100, true,
		newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
			&& battle.getBattle()->getMagicRules()["rulesetVersion"].Integer()
				== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION);
}
}
