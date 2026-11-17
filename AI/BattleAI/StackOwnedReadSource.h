/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <memory>
#include <optional>
#include <vector>
#include "../../lib/bonuses/Bonus.h"
namespace forecast { class NativeReadView; }
namespace forecastEffects { class State; }
namespace bonusDependencies { class CapturedModifiers; class OwnedBonusQueryFrame; class RecipientLayerSource; }
namespace battle { class CUnitState; }
class CCreature;
/// Fully prepared source replacement. No target fields change until publish.
struct PreparedOwnedReadSource
{
	std::shared_ptr<const forecast::NativeReadView> source;
	std::shared_ptr<const forecastEffects::State> effects;
	std::shared_ptr<const bonusDependencies::CapturedModifiers> program;
	std::shared_ptr<const bonusDependencies::RecipientLayerSource> sourceLease;
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> queries;
	std::optional<std::vector<Bonus>> spells;
	const battle::CUnitState * unit = nullptr;
	const CCreature * type = nullptr;
};
