/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class HypotheticBattle;
struct SetStackEffect;
namespace graveDustAI
{
/// Validation-only existing export-ledger branch. Does not import a player view,
/// commit effects or authorize a source interval. No ordinary live caller yet.
void validateOwnedEffectChanges(const HypotheticBattle & branch, const SetStackEffect & effects);
}
