/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../battle/HeroActionAllowanceState.h"
#include <optional>

class BattleInfo;
class CGHeroInstance;
struct BattleSpellCast;

struct DLL_LINKAGE AcceptedHeroSpellAllowanceResult
{
	bool automaticHeroCast;
	bool sharedActionBudget;
	bool spendsHeroAllowance;
	std::optional<HeroSpellAllowanceTransition::Result> transition;
};

/// Accepted hero cast stage, after caller validation of battle and paid Mana context.
/// Plans allowance changes locally before committing the ledger and legacy counters.
/// Cast counts, wards, sequence metadata, Warcasting and Mana remain caller-owned.
DLL_LINKAGE AcceptedHeroSpellAllowanceResult prepareAndCommitAcceptedHeroSpellAllowance(
	BattleInfo & battle, const CGHeroInstance * hero, const BattleSpellCast & pack);
