/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/battle/BattleHex.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <functional>

class BattleAction;
class BattleInterface;
struct BattleSpellCast;
struct StackAttackedInfo;

/// Action-local visual receipt; never infers a new target or changes spell effects.
class OverflowSpellPresentation
{
	SpellID manualSpell = SpellID::NONE;
	BattleSide manualSide = BattleSide::NONE;
	BattleHex primaryHex = BattleHex::INVALID;

public:
	void reset();
	void begin(const BattleAction & action);
	void remember(BattleInterface & owner, const BattleSpellCast & receipt);
	bool queue(BattleInterface & owner, const StackAttackedInfo & injury,
		std::function<void()> onArrival) const;
};
