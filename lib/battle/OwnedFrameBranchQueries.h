/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedUnitBonusFrame.h"
namespace battle
{
/// Bounded current-recipient bridge. This is NOT a native capture/import grant.
/// Every frame must be a fork in one captured batch; incoming values are consumed
/// immediately, never stored. Callers retain frames through every returned borrow.
/// Form changes, created occurrences and side-policy events require a further
/// source-domain transaction and are deliberately outside this initial bridge.
class DLL_LINKAGE OwnedFrameBranchQueries final
{
public:
	class PreparedRecords
	{
		friend class OwnedFrameBranchQueries;
		std::shared_ptr<const ownedBonusPayloads::Captured> owner;
		explicit PreparedRecords(std::shared_ptr<const ownedBonusPayloads::Captured> owner) : owner(std::move(owner)) {}
	};
	/// One finite metadata/alias pool for the ENTIRE producer vector. The caller
	/// reuses this value through ordered canonical replay; no per-record reclone.
	static PreparedRecords prepareRecords(const std::vector<Bonus> & records);
	struct CurrentUnit
	{
		std::shared_ptr<OwnedUnitBonusFrame> frame;
		const CUnitState * values = nullptr;
	};
	/// Prepare ALL scalar rows and opaque peers before allocation-free publication.
	/// A bad/duplicate/missing occurrence or unsupported form rejects the WHOLE
	/// roster. No callback, library, serialization codec or native node is used.
	static void synchronize(OwnedUnitBonusFrame & recipient, const std::vector<CurrentUnit> & ordered);
	static const IUnitEnvironment & environment(const OwnedUnitBonusFrame & recipient);
	static std::shared_ptr<CUnitState> acquireState(const std::shared_ptr<const OwnedUnitBonusFrame> & recipient);
	static CUnitState & effectQueryState(OwnedUnitBonusFrame & recipient);
	/// Exact source-export role ONLY; query/updater aliases never become exports.
	static BonusList sourceExports(const OwnedUnitBonusFrame & recipient);
	static void removeMatching(OwnedUnitBonusFrame & recipient, const CSelector & selector);
	/// Pure finite metadata copy; no CURRENT/default codec or entity lookup.
	static void append(OwnedUnitBonusFrame & recipient, const PreparedRecords & records, size_t index);
	static void refresh(OwnedUnitBonusFrame & recipient, const Bonus & record);
};
}
