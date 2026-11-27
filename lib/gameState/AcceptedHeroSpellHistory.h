/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class BattleInfo;
struct BattleSpellCast;
/// Called only after the accepted cast and its Hero Action receipt are validated.
void recordAcceptedHeroSpellHistory(BattleInfo &, const BattleSpellCast &);
