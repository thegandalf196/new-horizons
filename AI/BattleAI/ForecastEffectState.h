/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "../../lib/battle/BattleUnitTurnReason.h"
#include <cstdint>
#include <optional>
#include <map>
#include <span>

namespace forecastEffects
{
using RecordId = uint32_t;
enum class Kind : uint8_t { Numeric, Frozen, Bind, ForceLance, FocusManaged };
enum class Dirty : uint32_t
{
	None = 0, UnitStats = 1, HealthCapacity = 2, Queue = 4,
	Retaliation = 8, SpellDefense = 16, Control = 32
};
constexpr Dirty operator|(Dirty left, Dirty right)
{
	return static_cast<Dirty>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}
constexpr bool contains(Dirty value, Dirty bit)
{
	return (static_cast<uint32_t>(value) & static_cast<uint32_t>(bit)) != 0;
}
/// Native exporter identity, not a recipient-effective/selected record index.
/// A single owner must capture/age its timers once, never once per recipient.
/// occurrence includes native clock traversal/export occurrence, including
/// repeated diamond paths. Same payload under DIFFERENT origins is unsupported.
/// Same origin may repeat only for proved representation aliases of ONE clock.
struct Origin
{
	uint64_t owner = 0;
	uint64_t occurrence = 0;
	bool operator==(const Origin &) const = default;
};
struct RawRecord
{
	std::shared_ptr<const Bonus> value;
	Origin origin;
	/// Computed ONCE from the real round selector and source/group ownership.
	bool roundClock = false;
	/// Captured from native spell persistence, not inferred from duration.
	bool transientOnDeath = false;
};
struct Admission
{
	std::optional<SpellID> entangle;
	std::optional<SpellID> forceLance;
	std::optional<SpellID> focusMagic;
	int32_t round = 0;
	int32_t frozenApplicationRound = -1;
};
struct Record
{
	RecordId id = 0;
	Origin origin;
	Kind kind = Kind::Numeric;
	bool roundClock = false;
	bool transientOnDeath = false;
	std::shared_ptr<const Bonus> value;
};
struct Forfeit
{
	uint64_t activation = 0;
	BattleUnitTurnReason reason = BattleUnitTurnReason::TURN_QUEUE;
	bool completed = false;
};
class Transitions;
/// No provider, callback, RNG or rules documents. Each changed state deep-owns
/// admitted raw Bonus payloads; branch handles share immutable prior versions.
class DLL_LINKAGE State final
{
	friend class Transitions;
	std::map<RecordId, std::shared_ptr<const ownedBonusPayloads::Captured>> payloadOwners;
	std::vector<Record> records;
	Admission admission;
	std::optional<Forfeit> forfeit;
	uint64_t revisionValue = 0;
	uint64_t nextRecordId = 0;
	static Kind classify(const RawRecord &, const Admission &);
	static std::shared_ptr<State> copy(const State &);
public:
	/// Throws before publication for unsupported/opaque/updater/foreign clocks.
	static std::shared_ptr<const State> capture(std::span<const RawRecord>, Admission);
	std::span<const Record> values() const { return records; }
	const Record * find(RecordId) const;
	uint64_t revision() const { return revisionValue; }
	int32_t round() const { return admission.round; }
	int32_t frozenApplicationRound() const { return admission.frozenApplicationRound; }
	const std::optional<Forfeit> & pendingForfeit() const { return forfeit; }
	bool frozen() const;
};
}
