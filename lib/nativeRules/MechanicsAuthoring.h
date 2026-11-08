/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "PreparedBonusRule.h"
#include <functional>
#include <string>
#include <string_view>
class JsonNode;
class CCreature;
namespace nativeRules
{
enum class TimedSourceSlot : uint8_t { None, Sanctuary, Forgetfulness };
/// Constructed by the native source adapter, never from author-supplied identity.
struct DLL_LINKAGE ResolvedAuthoringSource
{
	SourceKind kind;
	BonusSourceID identity;
	std::string stacking;
	SpellID spell;
	TimedSourceSlot timedSlot = TimedSourceSlot::None;
};
using FamilyResolver = std::function<const CCreature &(const std::string &)>;
/// Live branch facts supplied by the existing timed owner, with no retained pointers.
struct DLL_LINKAGE TimedAdditionFacts
{
	SpellID spell;
	int32_t rounds = 0;
	bool sanctuaryKeeper = false;
	bool mindbreaker = false;
	bool nativeMagic = false;
};
class DLL_LINKAGE PreparedMechanics
{
	struct Operation
	{
		enum class Kind { Bonus, SanctuaryKeeper, CreatureSuppression } kind;
		PreparedBonusRule ordinary;
		std::optional<PreparedBonusRule> enhanced;
	};
	ResolvedAuthoringSource source;
	std::vector<Operation> ordered;
	PreparedMechanics(ResolvedAuthoringSource source, std::vector<Operation> ordered);
	friend DLL_LINKAGE PreparedMechanics prepareMechanics(const JsonNode &, ResolvedAuthoringSource, const FamilyResolver &);
public:
	/// Entire existing passive output is replaced, never appended to its legacy producer.
	std::vector<std::shared_ptr<Bonus>> persistentReplacement() const;
	/// Existing Timed owner retains cleanup, specialty scaling, order and application.
	std::vector<Bonus> timedAdditions(const TimedAdditionFacts & facts) const;
	std::string_view timedOrderingKey() const;
};
/// Cold preparation only; rejects unsupported input before returning any installable output.
DLL_LINKAGE PreparedMechanics prepareMechanics(const JsonNode & mechanics,
	ResolvedAuthoringSource source, const FamilyResolver & resolveFamily);
/// Absence alone selects the old producer. Present malformed/null/unsupported never falls back.
/// Caller must choose one source producer; this API does not attach or mutate game state.
DLL_LINKAGE std::optional<PreparedMechanics> prepareSourceReplacement(const JsonNode & capturedRow,
	ResolvedAuthoringSource source, const FamilyResolver & resolveFamily);
}
