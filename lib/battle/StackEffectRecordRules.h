/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"
class BonusList;
namespace battle { class Unit; class CUnitState; }
namespace stackEffectRecordRules
{
/// Incoming lifecycle records are handled before this ordinary admission gate.
DLL_LINKAGE bool incomingProtected(const battle::Unit & unit, const Bonus & value);
DLL_LINKAGE bool removalProtected(const battle::Unit & unit, const Bonus & value);
DLL_LINKAGE std::shared_ptr<Bonus> previousSwiftLifecycle(const battle::Unit & unit,
	const Bonus & value, bool forceAdd, const BonusList & localExports);
struct PhysicalMarkerReplacement { Bonus marker; bool replaceExisting; };
/// Caller validates marker metadata before the Time Stop admission gate.
DLL_LINKAGE PhysicalMarkerReplacement physicalMarkerReplacement(const Bonus & value,
	const BonusList & localExports);
DLL_LINKAGE CSelector physicalMarkerGroup(const Bonus & value);
struct GuardianPool { int64_t hitPoints; int32_t rounds; };
DLL_LINKAGE GuardianPool guardianPool(const Bonus & value);
DLL_LINKAGE bool selectsCurrentFormMarker(const battle::CUnitState & unit, const Bonus & value);
DLL_LINKAGE bool removesConfusion(const battle::Unit & unit, const Bonus & selected);
DLL_LINKAGE void finishConfusionRemoval(battle::CUnitState & unit, bool selected);
DLL_LINKAGE void restoreGuardianAfterRemoval(battle::CUnitState & unit);
}
