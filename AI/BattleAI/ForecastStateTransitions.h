/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastState.h"
#include <optional>
namespace battleSearch { class SearchBudget; }
namespace forecast
{
enum class Refusal { None, UnknownUnit, Unsupported, InvalidInput, Exhausted, Stale, Occupied };
struct Receipt
{
	Event event;
	UnitId unit;
	uint64_t beforeRevision = 0, afterRevision = 0;
	int64_t appliedHealth = 0;
	int32_t beforeCount = 0, afterCount = 0;
	BattleHex beforePosition, afterPosition;
	battle::HealInfo healing;
};
struct Result
{
	Refusal refusal = Refusal::None;
	std::optional<Receipt> receipt;
	explicit operator bool() const { return receipt.has_value(); }
};
/// Value-only proof from the retained native movement solver for the CURRENT
/// branch/revision. It is not an imported command or a gameplay permission.
class MovePermit
{
	friend class NativeAdapter;
	friend class Transitions;
	SourceIdentity source;
	std::shared_ptr<const void> layerIdentity; // Owning exact immutable geometry capability.
	uint64_t revision;
	UnitId unit;
	BattleHex from, to;
	MovePermit(SourceIdentity source, std::shared_ptr<const void> layerIdentity, uint64_t revision, UnitId unit, BattleHex from, BattleHex to)
		: source(std::move(source)), layerIdentity(std::move(layerIdentity)), revision(revision), unit(unit), from(from), to(to) {}
};
class DLL_LINKAGE Transitions
{
	static Refusal preflight(State &, UnitId, Event, battleSearch::SearchBudget *);
	static Result refuse(State &, Refusal);
public:
	static Result applyDamage(State &, UnitId, int64_t amount, battle::DamageProvenance,
		bool destroyRemains, battleSearch::SearchBudget *);
	static Result applyHealing(State &, UnitId, int64_t amount, EHealLevel, EHealPower,
		battleSearch::SearchBudget *);
	static Result move(State &, const MovePermit &, battleSearch::SearchBudget *);
	static Result consumeActivation(State &, UnitId, battleSearch::SearchBudget *);
	static Result consumeRetaliation(State &, UnitId, battleSearch::SearchBudget *);
	static Result afterAttack(State &, UnitId, bool ranged, bool counter, bool physical, battleSearch::SearchBudget *);
	static bool advanceRound(State &, bool openingRound, battleSearch::SearchBudget *, Refusal * failure = nullptr);
};
}
