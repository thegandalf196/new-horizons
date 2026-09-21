/*
 * NewHorizonsMagicNullAssert.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 * Build-owned standalone probe, never part of the ordinary test executable.
 */
#include "../../lib/StdInc.h"
#include "../../lib/GameSettings.h"

#ifdef NDEBUG
#error This probe requires assertions enabled and no Release PCH
#endif

int main(int argc, char ** argv)
{
	GameSettings settings;
	if(argc == 2 && std::string(argv[1]) == "--invalid-other")
	{
		// Run separately with core dumps disabled. Expected SIGABRT from the
		// locally compiled assertion-enabled GameSettings.cpp, never a PASS exit.
		(void)settings.getValue(EGameSettings::HEROES_NEW_HORIZONS);
		return 3;
	}
	if(settings.getMagicOverride().has_value())
		return 1;
	settings.addOverride(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	const auto copied = settings.getMagicOverride();
	if(!copied || !copied->isNull())
		return 1;
	if(!settings.getValue(EGameSettings::MAGIC_NEW_HORIZONS).isNull())
		return 1;
	return 0;
}
