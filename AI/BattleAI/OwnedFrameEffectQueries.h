/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/OwnedFrameBranchQueries.h"
#include "../../lib/battle/GraveDustEffectChanges.h"
#include "../../lib/battle/AccessibilityInfo.h"
#include <map>
class HypotheticBattle;
class StackWithBonuses;
struct SetStackEffect;
/// Private complete-current-roster replay. No source import or native fallback.
/// Input records share ONE finite owned metadata pool for the whole transaction.
/// Returned Units/Bonus aliases borrow this scope. Only publish() touches the
/// actual branch, after all canonical replay and peer/scalar preparation passes.
class OwnedFrameEffectQueries final
{
	const HypotheticBattle & owner;
	StackWithBonuses * target = nullptr;
	bool isolated = false;
	std::vector<std::shared_ptr<battle::OwnedUnitBonusFrame>> actual, order;
	std::map<uint32_t, std::shared_ptr<battle::OwnedUnitBonusFrame>> frames;
	std::shared_ptr<const ownedBonusPayloads::Captured> inputOwner;
	std::map<const Bonus *, size_t> inputRecords;
	bool dirty = true;
	void initialize(StackWithBonuses * target, const std::vector<const std::vector<Bonus> *> & records);
	void synchronize();
	std::shared_ptr<battle::OwnedUnitBonusFrame> frame(uint32_t id) const;
public:
	OwnedFrameEffectQueries(const HypotheticBattle & owner, StackWithBonuses & target, const std::vector<Bonus> & records);
	OwnedFrameEffectQueries(const HypotheticBattle & owner, const SetStackEffect & packet);
	void replay(uint32_t id, const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation);
	void publish();
	const battle::Unit * find(uint32_t id);
	const battle::Unit & current(uint32_t id);
	battle::CUnitState & state(uint32_t id);
	BonusList exportedRecords(uint32_t id);
	void appendExported(uint32_t id, const std::shared_ptr<Bonus> & value);
	void removeExported(uint32_t id, const std::shared_ptr<Bonus> & value);
	void removeMatchingExports(uint32_t id, const CSelector & selected);
	void refreshMatchingExports(uint32_t id, const Bonus & value);
	AccessibilityInfo accessibility(uint32_t id);
};
