/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "../../lib/bonuses/BonusEnum.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct Bonus;
class BonusList;
class CStack;
class CStackInstance;
class CCallback;
class IBonusBearer;

namespace creatureBonusStatusPresentation
{
/// Value-only rows; the creature window retains all widget ownership.
struct Row
{
	std::string description;
	ImagePath imagePath;
	BonusSource bonusSource;
	std::string tooltip;
};

/// Format the selected bonus cards without owning widgets or changing state.
std::vector<Row> visibleRows(const IBonusBearer & bonusSource, const CStackInstance & stackNode,
	const BonusList & visibleBonuses,
	const std::function<std::string(const std::shared_ptr<Bonus> &)> & describe);

bool isHydrasVitality(const Bonus & bonus);
std::string hydrasVitalityText(const CStack * battleStack, const Bonus & marker);
/// Supplemental live statuses precede the normal sorted bonus rows. The input
/// list is the original chosen markers, used to avoid a duplicate Hydra row.
std::vector<Row> supplementalRows(const CStack * battleStack, const CStackInstance * stackNode,
	const BonusList & visibleBonuses, CCallback * callback);
}
