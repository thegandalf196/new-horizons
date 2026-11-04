/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleSide.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <memory>
#include <vector>
class DamageCache;
class HypotheticBattle;
class CBattleInfoCallback;
namespace battle { class Unit; }
/// Shared existing Light-specialized utility. Bodies extracted without arithmetic
/// or traversal changes; ordinary paid casts retain their historical outputs.
float guardianSpiritMitigationValue(const battle::Unit *, const battle::Unit *,
	DamageCache &, const std::shared_ptr<HypotheticBattle> &);
float heavenlyGaleSavedDamageValue(const battle::Unit *, uint64_t, DamageCache &,
	const std::shared_ptr<HypotheticBattle> &);
float heavenlyGaleMitigationValue(BattleSide, const CBattleInfoCallback &,
	const std::shared_ptr<HypotheticBattle> &, SpellID, DamageCache &);
float divineRetributionThreatValue(const battle::Unit *, const battle::Unit *, BattleSide,
	const CBattleInfoCallback &, const std::shared_ptr<HypotheticBattle> &, SpellID, DamageCache &);
float visibleCreatureSpellThreat(const std::vector<const battle::Unit *> &, bool magicalOnly = false);
float holyArmorMitigationValue(BattleSide, const battle::Unit *, const battle::Unit *, SpellID,
	int64_t, float, DamageCache &, std::shared_ptr<CBattleInfoCallback>);
