/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "RecipientSpellEffectPreview.h"
namespace newHorizonsMagic
{
/// The seven native timed families, using the actual loaded bonus identity and
/// the ordinary producer's value before the shared Metamagic packet transform.
std::optional<RecipientSpellEffectPreview> recipientTimedSpellEffectPreview(
	const spells::Mechanics & mechanics, const battle::Unit & recipient);
}
