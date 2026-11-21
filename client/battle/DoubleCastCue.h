/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/battle/BattleSide.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <cstdint>
#include <string>

class CBattleInfoCallback;

/// Metamagic 6 (Double Cast) read-only presentation. The open offer and use
/// counters come from the battle state every callback sees; nothing here
/// changes them.
namespace doubleCastCue
{
struct Readout
{
	int maximum = 0;
	int remaining = 0;
	/// Grant ID of the open follow-up offer, or 0 when none is open.
	uint32_t offer = 0;
	SpellID firstSpell = SpellID::NONE;

	bool available() const { return maximum > 0; }
	bool offerOpen() const { return offer != 0; }
};

Readout read(const CBattleInfoCallback & battle, BattleSide side);
/// One battle-log line announcing an open offer.
std::string offerText(const Readout & readout, const std::string & heroName, const std::string & firstSpellName);
/// Spellbook button help while the offer is open.
std::string spellButtonHelp(const Readout & readout);
/// True when the open offer would pay for `spell` but it repeats the first spell.
/// The spellbook, quick-spell bar and its shortcuts keep that spell unselectable;
/// a missing or ended battle answers false and the authority still decides.
bool repeatsFirstSpell(const CBattleInfoCallback * battle, SpellID spell);
/// Short unavailable hint (status bar, quick-spell bar) and its full explanation.
std::string differentSpellHint();
std::string differentSpellHelp(SpellID firstSpell);
/// Hero status row: compact value and details.
std::string statusValue(const Readout & readout);
std::string statusDetails(const Readout & readout);
}
