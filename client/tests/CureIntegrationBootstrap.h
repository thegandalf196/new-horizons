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
#include "../../server/tests/CureFixture.h"

class CStack;
class CBattleInfoCallback;

/// GUI-thread, initial-only ordinary scenario setup; no battle or callback ownership.
class CureIntegrationBootstrap
{
	cureBootstrap::State state;
	std::shared_ptr<cureFixture::Context> fixture;
	bool replicaChecked = false;

public:
	explicit CureIntegrationBootstrap(const char * optIn);
	void update();
	void verifyReplicatedFixture(const CBattleInfoCallback & battle);
	void verifyFixtureTarget(const CStack & target, const CBattleInfoCallback & battle) const;
};
