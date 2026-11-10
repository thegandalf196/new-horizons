/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../../lib/entities/hero/NewHorizonsCommandRules.h"
class CGHeroInstance;
namespace NK2AI
{
/// One rank's immediate slot unlock / repeating entitlement value. No army budget.
float commandArmyUnlockValue(int64_t before, int64_t after);
std::optional<float> commandClassSkillScore(const CGHeroInstance & hero, SecondarySkill skill, bool main);
}
