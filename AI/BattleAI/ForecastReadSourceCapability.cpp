/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HypotheticBattle.h"
bool HypotheticBattle::forecastReadSource(const Subject & source)
{
	const auto * parent = dynamic_cast<const HypotheticBattle *>(source.get());
	return parent && (parent->compactForecast || parent->compactReadSession);
}
