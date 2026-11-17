/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include "../battle/BattleHexArray.h"
#include "../battle/Destination.h"
#include "../constants/EntityIdentifiers.h"
#include "effects/Effects.h"
#include <optional>
#include <span>
#include <utility>
#include <vector>

class CBattleInfoCallback;
class ServerCallback;
namespace spells { class Mechanics; }
namespace battle { class Unit; }
namespace newHorizonsMetamagic
{
enum class OverflowSupport { Inapplicable, Supported, Unsupported };
enum class OverflowProjectionOutcome { Nominal, PrimaryResisted, SpillResisted, BothLand };
struct DLL_LINKAGE OverflowPrimaryDamage
{
	uint32_t primary = 0;
	int64_t healthBefore = 0, postShieldBeforeCasualties = 0, appliedDamage = 0;
	bool defeated = false;
};
struct DLL_LINKAGE OverflowPreview
{
	uint32_t primary = 0, recipient = 0;
	PlayerColor casterOwner = PlayerColor::CANNOT_DETERMINE, recipientOwner = PlayerColor::CANNOT_DETERMINE;
	int64_t primaryHealthBefore = 0, postShieldPrimaryDamage = 0, primaryDamageIfLands = 0;
	int64_t rawSpill = 0, defendedSecondaryDamage = 0, damageIfLands = 0;
	int32_t primaryResistancePercent = 0, secondaryResistancePercent = 0;
	bool conditionalRebirth = false;
	/// Cascade second receipt, selected and defended before either injury (max one).
	std::vector<OverflowPreview> additionalRecipients;
};
struct DLL_LINKAGE OverflowCandidate
{
	uint32_t id;
	BattleHexArray footprint;
	bool livingHostile = false;
};
/// A cast-local value receipt. It is never serialized or stored in battle state.
struct DLL_LINKAGE OverflowState
{
	std::optional<uint32_t> primary;
	BattleHexArray footprint;
	int32_t percent = 0, primaryResistancePercent = 0;
	std::optional<OverflowPrimaryDamage> lethal;
	bool invalid = false, consumed = false, cascade = false;
	void observe(const OverflowPrimaryDamage &);
};
DLL_LINKAGE int64_t overflowDamage(int64_t postShieldBeforeCasualties, int64_t healthBefore, int percent);
DLL_LINKAGE std::optional<uint32_t> nearestOverflowEnemy(uint32_t primary, const BattleHexArray & footprint,
	std::span<const OverflowCandidate> candidates);
/// Supported with no returned preview means no overkill/recipient, not unknown.
/// postKillProjection admits known native post-hit context changes (aura or
/// Implosion displacement):
/// existing AI outcome kernels use the actual post-kill receipt for its chance.
/// It never admits opaque death/event/propagator code.
DLL_LINKAGE OverflowSupport overflowSupport(const spells::Mechanics &, const spells::Target &, bool postKillProjection = false);
/// BEFORE original effects. Only the native accepted original pipeline passes
/// acceptedOriginal=true; automatic/mirror/proxy/internal origins remain inert.
DLL_LINKAGE OverflowState prepareOverflow(const spells::Mechanics &, const spells::Target &,
	const spells::effects::Effects::EffectsToApply &, bool acceptedOriginal);
/// Same pure target/defense plan used by native completion and hover/AI. No RNG,
/// resource mutation, battle copies or castEval. Unsupported/random producers
/// have no certified preview. The original cast is still only a potential action.
DLL_LINKAGE std::optional<OverflowPreview> previewOverflow(const spells::Mechanics &, const spells::Target &);
/// AFTER original effects; consumes one opportunity even when its selected target
/// blocks the spill. Secondary damage never feeds this cast's original ledger.
DLL_LINKAGE std::optional<OverflowPreview> planOverflow(const CBattleInfoCallback &, const spells::Mechanics &, OverflowState &);
/// One native completion routing stage; projection controls are supplied only
/// by castEval, while real casts use Nominal and the native resistance ledger.
DLL_LINKAGE std::optional<OverflowPreview> completeOverflow(ServerCallback &, const CBattleInfoCallback &,
	const spells::Mechanics &, OverflowState &, OverflowProjectionOutcome);
/// Existing native damage application, including Health/shields/remains/Rebirth.
/// No resistance roll, cast, action, original-effect rider or recursive Overflow.
/// Intended for native completion and existing AI weighted native outcome kernels.
DLL_LINKAGE std::pair<int64_t, int64_t> applyOverflowDamage(ServerCallback &, const CBattleInfoCallback &, const OverflowPreview &);
}
