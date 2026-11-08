/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
class DamageCache;
class HypotheticBattle;
namespace battle { class Unit; }
/// Existing ordinary Misfortune kernel: expected extra Death Blow damage only.
/// Shared with the bounded reflected-stat forecast; no new proc simulation.
float expectedMisfortuneDeathBlowTargetActivationValue(const battle::Unit * original,
	const battle::Unit * projected, DamageCache &, const std::shared_ptr<HypotheticBattle> &);
