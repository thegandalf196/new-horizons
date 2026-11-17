/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpellEffect.h"
#include "StatusHelpers.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsSorcery.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../scripting/ScriptTypeDescription.h"

namespace spells::effects::native
{
namespace
{
class Purify final : public SpellEffect
{
public:
	bool applicableTarget(Problem &, const Mechanics *, const Target & target) const override { return target.size() == 1; }
	Target transformTarget(const Mechanics *, const Target & aim, const Target &) const override { return aim; }
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createLocationUtilityEffect(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	if(description.sourcePath == "spells/purify") return std::make_shared<Purify>();
	return {};
}
}
