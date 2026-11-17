/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsMetamagicResolution.h"
#include <map>
#include <span>

class CBattleInfoCallback;
class ServerCallback;
class CSpell;
struct BattleStackAttacked;
namespace spells { class Mechanics; }

namespace newHorizonsMetamagic
{
struct DLL_LINKAGE RecipientSnapshot
{
	battle::BattleEffectSnapshot effects;
	/// Allegiance when the original cast was prepared, before control or transformation.
	PlayerColor originalOwner = PlayerColor::CANNOT_DETERMINE;
};
using RecipientSnapshots = std::map<uint32_t, RecipientSnapshot>;
struct DLL_LINKAGE CompletionReceipt
{
	int64_t removedMana = 0;
	struct RemovedEffect
	{
		uint32_t recipient;
		SpellID family;
		ResolutionKind kind;
	};
	std::vector<RemovedEffect> removedEffects;
};
struct DLL_LINKAGE ManaPayment
{
	int32_t normal = 0;
	int32_t permanentBuffer = 0;
	int32_t temporaryBuffer = 0;
	int64_t total() const { return static_cast<int64_t>(normal) + permanentBuffer + temporaryBuffer; }
};
DLL_LINKAGE RecipientSnapshots captureRecipients(const CBattleInfoCallback &, const spells::Mechanics &);
DLL_LINKAGE bool singleTargetFormula(const CSpell &, int32_t mastery);
/// Guaranteed consequences are shared by authoritative and detached castEval paths.
DLL_LINKAGE void applyGuaranteedCompletion(ServerCallback &, const CBattleInfoCallback &,
	const spells::Mechanics &, std::span<const uint32_t> resolved, std::span<const BattleStackAttacked> injuries);
/// Route through the original server delegate after effects, not the cast recorder.
DLL_LINKAGE CompletionReceipt completeCast(ServerCallback &, const CBattleInfoCallback &,
	const spells::Mechanics &, const RecipientSnapshots &, std::span<const uint32_t> resolved,
	std::span<const BattleStackAttacked> injuries);
/// Call only after actual Mana payment and previously awarded refund accounting.
DLL_LINKAGE int32_t refundCast(ServerCallback &, const CBattleInfoCallback &, const spells::Mechanics &,
	const CompletionReceipt &, const ManaPayment & actualPaid, const ManaPayment & alreadyRefunded,
	bool validSingleTarget, bool completelyResisted);
}
