/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastState.h"
#include "../../lib/battle/Destination.h"
#include <vcmi/spells/Magic.h>
class CBattleInfoCallback;
class PossibleSpellcast;
namespace spells { class Mechanics; namespace effects { class Effect; } }
namespace forecast::heroAction
{
enum class Route : uint8_t { Compact, CurrentNativeReference };
enum class Reason : uint8_t
{
	None, SourceNotAdmitted, InvisibleHero, SideState, UnsupportedOrder,
	AutomaticCompletion, CustomMechanics, UnsupportedEffect, UnprovenTimedPayload,
	StochasticCompletion, UnsupportedCompletion, MissingEventRecipe, InvalidTarget, UnsafeTargeting, WrongCastProvenance
};
struct Decision
{
	Route route = Route::CurrentNativeReference;
	Reason reason = Reason::SourceNotAdmitted;
	EventMask required = 0;
};
/// Narrows an already admitted CURRENT source; never widens NativeAdapter's
/// source/perk/lifecycle proof. Call BEFORE construction of a candidate branch,
/// StartAction cleanup, allowance/payment or any effect prefix. Reference means
/// evaluate the same candidate from the current source, never omit the action
/// or reset a nested source to the original real battle. No action permission.
DLL_LINKAGE Decision classify(const CBattleInfoCallback &, BattleSide, const PossibleSpellcast &);
/// Effects-only gate for an existing creature cast from the CURRENT admitted
/// source. Native mechanics prove creature mode and exact current caster identity.
/// No Hero/Side/Mana/
/// Metamagic completion or resource-callback semantics are added or changed.
DLL_LINKAGE Decision classifyCreature(const CBattleInfoCallback &, const spells::Mechanics &, const spells::Target &);
/// Exact native implementation identity; enum/name overrides alone confer no
/// authority. Shared with focused tests and checked before target callbacks.
DLL_LINKAGE bool supportedImplementation(const spells::effects::Effect &);
/// Pure native effect-recipe inspection shared by the admission entrypoints.
/// No source/side/caster authority or action permission is conferred.
DLL_LINKAGE Reason inspectSupportedEffects(const spells::Mechanics &, const spells::Target &, EventMask & required);
DLL_LINKAGE const char * reasonText(Reason);
}
