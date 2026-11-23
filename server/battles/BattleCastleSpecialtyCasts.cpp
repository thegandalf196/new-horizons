/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CastleSpecialtyPrayers.h"
#include "../../lib/battle/ConsecratedArmsChoice.h"
#include "../../lib/battle/ConsecratedArmsOrder.h"
#include "../../lib/battle/NewHorizonsFrostMantle.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/entities/hero/NewHorizonsCastleSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"

namespace
{
void logLine(CGameHandler & handler, const CBattleInfoCallback & battle, const std::string & text, const battle::Unit * unit)
{
	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();
	MetaString line;
	line.appendRawString(text);
	if(unit)
	{
		line.replaceNumber(unit->getCount());
		line.replaceName(unit->creatureId(), unit->getCount());
	}
	message.lines.push_back(std::move(line));
	handler.sendAndApply(message);
}

void grantSpecialtyPrayers(CGameHandler & handler, const CBattleInfoCallback & battle, BattleSide side,
	uint32_t recipient, const std::vector<uint8_t> & choices, const char * text)
{
	const auto * hero = battle.battleGetFightingHero(side);
	const auto policy = consecratedArms::capturePolicy(hero);
	const auto prayers = consecratedArms::chosenPrayerList(choices);
	if(!hero || !policy.rank || prayers.empty() || consecratedArms::prayerHolders(battle, side, {recipient}).empty())
		return;
	const int faith = consecratedArms::actualFaith(*hero);
	const int potency = newHorizonsHeroes::boostedPotencyFaith(consecratedArms::potencyFaith(faith, policy),
		newHorizonsHeroes::specialtyPrayerPotencyPercent(hero));
	auto effects = consecratedArms::specialtyPrayerEffects(battle, side, recipient, prayers, policy, faith, potency);
	if(effects.toAdd.empty())
		return;
	handler.sendAndApply(effects);
	logLine(handler, battle, text, battle.battleGetUnitByID(recipient));
}

void replaceBenediction(CGameHandler & handler, const CBattleInfoCallback & battle, BattleSide side,
	std::optional<uint32_t> blessed, const CGHeroInstance & hero)
{
	SetStackEffect update;
	update.battleID = battle.getBattle()->getBattleID();
	for(const auto id : castleSpecialtyPrayers::benedictionHolders(battle, side))
	{
		const auto * unit = battle.battleGetUnitByID(id);
		std::vector<Bonus> markers;
		// Hold the list: ranging over *temporary leaves it dangling before C++23.
		const auto held = unit->getBonuses(CSelector([](const Bonus * candidate)
			{ return castleSpecialtyPrayers::isBenedictionMarker(candidate); }));
		for(const auto & bonus : *held)
			markers.push_back(*bonus);
		update.toRemove.emplace_back(id, std::move(markers));
	}
	if(blessed)
		if(const auto marker = castleSpecialtyPrayers::benedictionMarker(&hero))
			update.toAdd.emplace_back(*blessed, std::vector<Bonus>{*marker});
	if(!update.toRemove.empty() || !update.toAdd.empty())
		handler.sendAndApply(update);
}
}

BattleActionProcessor::CastleCastContext BattleActionProcessor::prepareCastleSpecialtyCast(
	const CBattleInfoCallback & battle, const CSpell & spell, const battle::Target & target) const
{
	CastleCastContext result;
	if(target.empty())
		return result;
	if(spell.getId() == SpellID::FROST_RING && target.front().hexValue.isValid())
		if(const auto * centre = battle.battleGetUnitByPos(target.front().hexValue, true))
			result.centre = centre->unitId();
	if(target.size() == 1 && target.front().unitValue)
		result.unitTarget = target.front().unitValue->unitId();
	return result;
}

void BattleActionProcessor::completeCastleSpecialtyCast(const CBattleInfoCallback & battle, const BattleAction & ba,
	const CGHeroInstance & hero, const CSpell & spell, const CastleCastContext & context)
{
	const auto side = ba.side;
	// Benediction of Arms: Adela's latest single-target Bless marks its stack.
	if(spell.getId() == SpellID::BLESS && newHorizonsHeroes::benedictionOfArmsRule(&hero) && context.unitTarget)
	{
		const auto * blessed = battle.battleGetUnitByID(*context.unitTarget);
		const bool friendly = blessed && blessed->alive() && blessed->unitSide() == side
			&& blessed->hasBonusFrom(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)));
		if(friendly)
			replaceBenediction(*gameHandler, battle, side, blessed->unitId(), hero);
	}
	// Frost Mantle: cloak a friendly centre stack, or only chill an enemy one receptive to Frost Ring.
	if(spell.getId() == SpellID::FROST_RING && context.centre)
	{
		const auto * centre = battle.battleGetUnitByID(*context.centre);
		const auto effect = !centre || !centre->alive() ? std::nullopt
			: centre->unitSide() == side ? newHorizonsFrostMantle::cloak(&hero)
			: newHorizonsFrostMantle::chills(battle, hero, *centre) ? newHorizonsFrostMantle::chill(&hero) : std::nullopt;
		if(effect)
		{
			SetStackEffect update;
			update.battleID = battle.getBattle()->getBattleID();
			update.toUpdate.emplace_back(centre->unitId(), std::vector<Bonus>{*effect});
			gameHandler->sendAndApply(update);
			logLine(*gameHandler, battle, centre->unitSide() == side
				? "Frost Mantle cloaks %d %s." : "Frost Mantle chills %d %s.", centre);
		}
	}
	// Sermon: the Light spell's chosen prayer, admitted before StartAction.
	if(castleSpecialtyPrayers::sermonCast(battle, ba) && ba.prayerRecipient != BattleAction::NO_PRAYER_RECIPIENT)
		grantSpecialtyPrayers(*gameHandler, battle, side, ba.prayerRecipient, ba.prayerChoices,
			"Sermon grants %d %s a Consecrated Arms prayer.");
}

void BattleActionProcessor::grantBenediction(const CBattleInfoCallback & battle, const BattleAction & ba,
	const std::vector<uint8_t> & prayerChoices)
{
	const auto * hero = battle.battleGetFightingHero(ba.side);
	if(!newHorizonsHeroes::benedictionOfArmsRule(hero))
		return;
	const auto blessed = castleSpecialtyPrayers::benedictionRecipient(battle, ba.side);
	if(blessed && !prayerChoices.empty())
		grantSpecialtyPrayers(*gameHandler, battle, ba.side, *blessed, prayerChoices,
			"Benediction of Arms also blesses %d %s with the prayer.");
	replaceBenediction(*gameHandler, battle, ba.side, std::nullopt, *hero);
}
