/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/filesystem/ResourcePath.h"

namespace poisonPresentation
{
inline constexpr auto RED_CLOUD = "NH_broghild_poison_red";

/// The accepted cast receipt selects its presentation; old recipient bonuses
/// cannot turn a newly accepted ordinary cast red.
inline AnimationPath acceptedCastOverride(SpellID spell, bool strongWyvernPoison)
{
	return spell == SpellID(SpellID::POISON) && strongWyvernPoison
		? AnimationPath::builtin(RED_CLOUD) : AnimationPath{};
}

/// Replace only the canonical poison cloud. Other queue entries, pauses,
/// positions, translucency and sound remain owned by ordinary presentation.
inline AnimationPath queueResource(SpellID spell, const AnimationPath & ordinary,
	const AnimationPath & resourceOverride)
{
	if(spell == SpellID(SpellID::POISON) && ordinary == AnimationPath::builtin("SP11_")
		&& resourceOverride == AnimationPath::builtin(RED_CLOUD))
		return resourceOverride;
	return ordinary;
}
}
