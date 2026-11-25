/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <cstdint>

namespace battle
{
class CHealth;

/// Next value of the process-wide revision counter (relaxed atomic increment).
DLL_LINKAGE uint64_t nextUnitStateRevision() noexcept;

/// Runtime-only plain-state revision of one CUnitState object.
///
/// Contract:
/// - The value increases whenever the unit's plain state may have changed:
///   its position; its whole health ledger (count, first-creature HP, capacity
///   cohorts, temporary HP, resurrected, remains and casualty records); ghost
///   and ghostPending (and therefore alive()); its battle form (creature,
///   rounds, pending restoration, original health and view revision); and its
///   Phantom integrity. Whole-state transfers change it too: assignment,
///   prepared exchange (on both units), snapshot restore, reset/localInit and
///   JSON load.
/// - It may also increase when nothing changed; it never decreases.
/// - Every value, including a new object's first one, is drawn from one
///   process-wide counter, so no two objects and no two moments ever share a
///   value: an object reallocated at a freed unit's address never repeats one
///   of its revisions. Values carry no gameplay meaning and differ between runs.
/// - It belongs to the object: assignment and exchange never copy it, and it is
///   never saved, serialized or sent.
/// - Each write is followed by a bump before the unit is queried again, so a
///   query between two writes of one operation sees its own value. A health
///   ledger operation, which itself queries its unit, bumps when it starts and
///   again when it ends (HealthEdit, StateRevisionScope): a value read while it
///   runs differs from every value read before or after it.
/// - Not covered: the activation, round and Wait flags, ammunition, shields and
///   the other public fields of CUnitState, and the unit's bonus tree (use
///   getTreeVersion() for that).
/// CUnitState routes every write of a covered field through its mutators; the
/// repository check CI/check_unit_state_writes.py rejects direct writes.
class UnitStateRevision
{
public:
	UnitStateRevision() noexcept : revision(nextUnitStateRevision()) {}
	UnitStateRevision(const UnitStateRevision &) = delete;
	UnitStateRevision & operator=(const UnitStateRevision &) = delete;
	uint64_t value() const noexcept { return revision; }
	void bump() noexcept { revision = nextUnitStateRevision(); }

private:
	uint64_t revision;
};

/// Bumps a revision on construction and again on destruction.
class [[nodiscard]] StateRevisionScope
{
public:
	explicit StateRevisionScope(UnitStateRevision & changed) noexcept : revision(changed) { revision.bump(); }
	~StateRevisionScope() { revision.bump(); }
	StateRevisionScope(const StateRevisionScope &) = delete;
	StateRevisionScope & operator=(const StateRevisionScope &) = delete;

private:
	UnitStateRevision & revision;
};

/// Write access to a unit's health ledger for one full expression:
/// unit.editHealth()->damage(amount). Never keep the handle in a variable.
class [[nodiscard]] HealthEdit
{
public:
	HealthEdit(CHealth & edited, UnitStateRevision & revision) noexcept : health(edited), scope(revision) {}
	CHealth * operator->() const noexcept { return &health; }

private:
	CHealth & health;
	StateRevisionScope scope;
};
}
