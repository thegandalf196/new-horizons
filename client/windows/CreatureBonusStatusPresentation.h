/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "../../lib/bonuses/BonusEnum.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <string>
#include <vector>

struct Bonus;
class BonusList;
class CStack;
class CStackInstance;
class CCallback;

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

bool isHydrasVitality(const Bonus & bonus);
std::string hydrasVitalityText(const CStack * battleStack, const Bonus & marker);
/// Supplemental live statuses precede the normal sorted bonus rows. The input
/// list is the original chosen markers, used to avoid a duplicate Hydra row.
std::vector<Row> supplementalRows(const CStack * battleStack, const CStackInstance * stackNode,
	const BonusList & visibleBonuses, CCallback * callback);
}
