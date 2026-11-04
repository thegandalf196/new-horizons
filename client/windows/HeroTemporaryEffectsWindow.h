/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CWindowObject.h"

class CGHeroInstance;

class HeroTemporaryEffectsWindow final : public CWindowObject
{
	std::vector<std::shared_ptr<CIntObject>> elements;

public:
	explicit HeroTemporaryEffectsWindow(const CGHeroInstance & hero);
};
