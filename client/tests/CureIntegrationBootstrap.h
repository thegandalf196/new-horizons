/*
 * CureIntegrationBootstrap.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#ifndef NH_CURE_CLIENT_INTEGRATION
#error Cure bootstrap belongs only to the dedicated integration executable
#endif

#include "CureIntegrationBootstrapState.h"

/// GUI-thread, initial-only ordinary scenario setup; no battle or callback ownership.
class CureIntegrationBootstrap
{
	cureBootstrap::State state;

public:
	explicit CureIntegrationBootstrap(const char * optIn) : state(optIn) {}
	void update();
};
