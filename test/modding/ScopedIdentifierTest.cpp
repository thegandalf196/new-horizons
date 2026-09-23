/*
 * ScopedIdentifierTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"

namespace test
{

TEST(ScopedIdentifierTest, QualifiedDuplicateNamesResolveAndUnqualifiedNameIsRejected)
{
	ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), std::string("vcmi-test")));

	CIdentifierStorage identifiers;
	identifiers.registerObject(ModScope::scopeBuiltin(), "skill", "identifierScopeProbe", 37);
	identifiers.registerObject("vcmi-test", "skill", "identifierScopeProbe", 73);
	identifiers.finalize();

	const auto coreID = identifiers.getIdentifier(ModScope::scopeGame(), "core:skill.identifierScopeProbe");
	const auto testModID = identifiers.getIdentifier(ModScope::scopeGame(), "vcmi-test:skill.identifierScopeProbe");
	ASSERT_TRUE(coreID.has_value());
	ASSERT_TRUE(testModID.has_value());
	EXPECT_EQ(*coreID, 37);
	EXPECT_EQ(*testModID, 73);
	EXPECT_FALSE(identifiers.getIdentifier(ModScope::scopeGame(), "skill", "identifierScopeProbe").has_value());
}

}
