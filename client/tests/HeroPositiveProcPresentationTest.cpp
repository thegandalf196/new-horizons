/* HeroPositiveProcPresentationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../battle/HeroPositiveProcPresentation.h"

namespace
{
constexpr bool everyFlagCombination()
{
	for(unsigned flags = 0; flags < 16; ++flags)
	{
		const auto cues = heroPositiveProcPresentation::effects(flags & 1, flags & 2, flags & 4, flags & 8);
		if(cues[0] != ((flags & 7) ? EBattleEffect::GOOD_LUCK : EBattleEffect::INVALID)
			|| cues[1] != ((flags & 8) ? EBattleEffect::GOOD_MORALE : EBattleEffect::INVALID))
			return false;
	}
	return true;
}
static_assert(everyFlagCombination());
static_assert(heroPositiveProcPresentation::effects(false, true, true, false)[0] == EBattleEffect::GOOD_LUCK);
static_assert(heroPositiveProcPresentation::effects(true, false, false, true)[1] == EBattleEffect::GOOD_MORALE);

bool firstDrawMarkerIsPerCueAndOnceOnly()
{
	heroPositiveProcPresentation::TraceContext original;
	if(!original.markFirstDrawReturned() || original.markFirstDrawReturned())
		return false;
	heroPositiveProcPresentation::TraceContext next;
	return next.markFirstDrawReturned() && !next.markFirstDrawReturned();
}
}

int main()
{
	return everyFlagCombination() && firstDrawMarkerIsPerCueAndOnceOnly() ? 0 : 1;
}
