/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "DoubleCastCue.h"
#include "BattleConsole.h"
#include "BattleInterface.h"
#include "BattleWindow.h"

#include "../GameInstance.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/MetamagicDoubleCast.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/texts/MetaString.h"

namespace doubleCastCue
{
namespace
{
std::string translated(MetaString text)
{
	return text.toString(&GAME->translator());
}

MetaString uses(const char * textID, const Readout & readout)
{
	auto text = MetaString::createFromTextID(textID);
	text.replaceNumber(readout.remaining);
	text.replaceNumber(readout.maximum);
	return text;
}
}

Readout read(const CBattleInfoCallback & battle, BattleSide side)
{
	Readout result;
	const auto * info = battle.getBattle();
	if(!info || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return result;
	const auto * hero = battle.battleGetFightingHero(side);
	result.maximum = metamagicDoubleCast::maximumUses(hero);
	if(!result.maximum)
		return result;
	const auto & state = info->getMetamagicDoubleCastState(side);
	result.remaining = metamagicDoubleCast::remainingUses(hero, state);
	result.offer = state.offerGrantId;
	result.firstSpell = state.firstSpell;
	return result;
}

std::string offerText(const Readout & readout, const std::string & heroName, const std::string & firstSpellName)
{
	auto text = MetaString::createFromTextID("new-horizons.doubleCast.offerOpen");
	text.replaceRawString(heroName);
	text.replaceRawString(firstSpellName);
	text.replaceNumber(readout.remaining);
	text.replaceNumber(readout.maximum);
	return translated(text);
}

std::string spellButtonHelp(const Readout & readout)
{
	return translated(uses("new-horizons.doubleCast.spellButton.help", readout));
}

bool repeatsFirstSpell(const CBattleInfoCallback * battle, SpellID spell)
{
	try
	{
		return battle && battle->getBattle()
			&& metamagicDoubleCast::followUpRepeatsFirstSpell(*battle, battle->battleGetMySide(), spell);
	}
	catch(const std::runtime_error &)
	{
		return false; // The callback registry may already have removed an ended battle.
	}
}

std::string differentSpellHint()
{
	return translated(MetaString::createFromTextID("new-horizons.doubleCast.differentSpell"));
}

std::string differentSpellHelp(SpellID firstSpell)
{
	auto text = MetaString::createFromTextID("new-horizons.doubleCast.differentSpell.help");
	const auto * spell = firstSpell.toSpell();
	text.replaceRawString(spell ? spell->getNameTranslated() : std::string());
	return translated(text);
}

std::string statusValue(const Readout & readout)
{
	return std::to_string(readout.remaining) + "/" + std::to_string(readout.maximum);
}

std::string statusDetails(const Readout & readout)
{
	auto details = translated(uses("new-horizons.doubleCast.status", readout));
	if(readout.offerOpen())
		details += "\n\n" + translated(MetaString::createFromTextID("new-horizons.doubleCast.status.offerOpen"));
	return details;
}
}

void BattleWindow::updateDoubleCastCue()
{
	const auto battle = owner.getBattle();
	if(!battle)
		return;
	const auto side = battle->battleGetMySide();
	const auto readout = doubleCastCue::read(*battle, side);
	auto spellButton = widget<CButton>("cast");
	const bool open = readout.offerOpen();
	if(spellButton && open != doubleCastCueShown)
	{
		spellButton->setBorderColor(open ? std::optional<ColorRGBA>(Colors::YELLOW) : std::nullopt);
		if(open)
			spellButton->setHelp(CButton::tooltip(
				MetaString::createFromTextID("new-horizons.doubleCast.spellButton.hover").toString(&GAME->translator()),
				doubleCastCue::spellButtonHelp(readout)));
		else
			spellButton->setHelp(CButton::tooltipLocalized("core.help.385"));
	}
	doubleCastCueShown = open;
	if(!open || readout.offer == announcedDoubleCastOffer)
		return;
	// One log line per offer; the battle state, not this cache, decides availability.
	announcedDoubleCastOffer = readout.offer;
	const auto * hero = battle->battleGetFightingHero(side);
	const auto * spell = readout.firstSpell.toSpell();
	if(console && hero)
		console->addText(doubleCastCue::offerText(readout, GAME->translator().translate(hero->getNameTextID()),
			spell ? spell->getNameTranslated() : std::string()));
}
