/*
 * main.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CVcmiTestConfig.h"
#include "../lib/filesystem/CMemoryBuffer.h"
#include "../lib/logging/CBasicLogConfigurator.h"
#include "../lib/VCMIDirs.h"

int main(int argc, char * argv[])
{
	::testing::InitGoogleTest(&argc, argv);
	// Keep native content/schema diagnostics in the test profile instead of
	// silently running fixtures after a mod failed validation during bootstrap.
	CBasicLogConfigurator logging(VCMIDirs::get().userLogsPath() / "VCMI_Test_log.txt", nullptr);
	logging.configureDefault();
	::testing::AddGlobalTestEnvironment(new CVcmiTestConfig());
	return RUN_ALL_TESTS();
}
