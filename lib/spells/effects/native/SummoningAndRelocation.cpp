/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummoningAndRelocation.h"
#include "SummonHelpers.h"
#include "../../../scripting/ScriptTypeDescription.h"

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createSummoningAndRelocationEffect(const ScriptTypeDescription & description)
{
	if(description.kind != ScriptKind::SPELL_EFFECT || !description.patches.empty()) return {};
	using Factory = std::shared_ptr<spells::effects::Effect> (*)(const std::string &);
	for(const Factory factory : {createRelocationEffect, createPhantomArmyEffect, createSummonEffect,
		createTimedSummonEffect, createDemonSummonEffect, createReanimateEffect, createSacrificeEffect,
		createNatureHealthEffect, createOrderedNatureEffect, createVerdantPrisonEffect, createTransfigureMatterEffect})
		if(auto result = factory(description.sourcePath)) return result;
	return {};
}
}
