/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include "../../lib/battle/VisibleHeroPolicy.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/battle/SpellPointBattleTransitions.h"
#include "../../lib/battle/WisdomCombatState.h"
#include "../../lib/battle/SpellResponseState.h"
#include "../../lib/battle/OverwhelmingFormulaState.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <memory>
#include <optional>
class CBattleInfoCallback;
namespace nativeRules { class CapturedRuleDocument; class CapturedPerkRules; }
namespace forecast
{
struct HeroLedger
{
	HeroActionAllowanceState allowances;
	WisdomCombatState wisdom;
	SpellResponseState response;
	OverwhelmingFormulaState overwhelming;
	int64_t residualLifeDamage = 0;
	int32_t residualLifeThreshold = 0, residualLifeLimit = 0;
	bool completedHeroCast = false;
	uint8_t completedLevelMask = 0;
	std::optional<int32_t> completedCastCount;
};
struct CastCharge
{
	uint64_t token = 0;
	SpellID spell;
	bool automatic = false;
	battle::spellPoints::Payment paid, refunded;
};
/// No native hero/unit/provider/callback address is retained. Immutable owners
/// identify exact admitted rules; all mutable ledgers belong to one branch.
class DLL_LINKAGE SideState
{
public:
	BattleSide side = BattleSide::NONE;
	PlayerColor controller = PlayerColor::CANNOT_DETERMINE;
	VisibleHeroPolicy::Knowledge knowledge = VisibleHeroPolicy::Knowledge::UNAVAILABLE;
	std::optional<ObjectInstanceID> hero;
	std::shared_ptr<const nativeRules::CapturedRuleDocument> magicOwner;
	std::shared_ptr<const nativeRules::CapturedPerkRules> perkOwner;
	std::optional<battle::spellPoints::State> mana;
	std::optional<HeroLedger> ledger;
	bool typedActions = false;
	newHorizonsElementalAttunement::Profile attunement;
	bool attunementKnown = false;
	uint8_t attunementMask = 0;
	int32_t round = 0;
	uint64_t revision = 0, nextCastToken = 1;
	std::vector<CastCharge> castCharges;
	static std::optional<SideState> capture(const CBattleInfoCallback &, BattleSide);
	SideState fork() const { return *this; }
	bool hasVisibleHero() const { return knowledge == VisibleHeroPolicy::Knowledge::VISIBLE_HERO; }
	bool knowsSpell(SpellID spell) const;
	void validate() const;
};
}
