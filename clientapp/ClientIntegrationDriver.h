/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#if defined(NH_CURE_CLIENT_INTEGRATION) && defined(NH_OWNED_PREPARATION_INTEGRATION)
#error Integration executables must select exactly one driver
#elif defined(NH_CURE_CLIENT_INTEGRATION)
#include "../client/tests/CureIntegrationDriver.h"
using ClientIntegrationDriver = CureIntegrationDriver;
#elif defined(NH_OWNED_PREPARATION_INTEGRATION)
#include "../client/tests/OwnedPreparationIntegrationDriver.h"
using ClientIntegrationDriver = OwnedPreparationIntegrationDriver;
#endif
