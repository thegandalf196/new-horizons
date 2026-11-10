/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CanonicalOrderValue.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/HeroCommandCoefficient.h"
#include "../../lib/bonuses/BonusSelector.h"

namespace heroOrderValue
{
float CanonicalOrderValue::coefficient(const char * commandKey, const char * effectKey) const
{
	const auto & formula = (*commandRules)[commandKey]["effects"][effectKey];
	return static_cast<float>(hero ? heroCommands::coefficient(formula,
		heroCommands::CoefficientInputs::projected(*hero,
			battle.battleGetHeroBonuses(side, Selector::type()(BonusType::PRIMARY_SKILL))),
		divineMandateEfficiencyBonusPercent) : heroCommands::coefficient(formula, 0, 0));
}
}
