/*
 * BattleResultReplyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../client/battle/BattleResultReply.h"
#include <stdexcept>

TEST(BattleResultReplyTest, AbsentQueryDoesNotSendEitherResultChoice)
{
	unsigned calls = 0;
	const auto sender = [&](int, QueryID) { ++calls; };
	for(int choice : {0, 1})
		sendBattleResultReply(choice, QueryID::NONE, sender);
	EXPECT_EQ(calls, 0u);
}

TEST(BattleResultReplyTest, ValidQuerySendsExactlyOneUnchangedReplyPerChoice)
{
	const QueryID query(17);
	for(int choice : {0, 1})
	{
		unsigned calls = 0;
		sendBattleResultReply(choice, query, [&](int actualChoice, QueryID actualQuery)
		{
			++calls;
			EXPECT_EQ(actualChoice, choice);
			EXPECT_EQ(actualQuery, query);
		});
		EXPECT_EQ(calls, 1u);
	}
}

TEST(BattleResultReplyTest, NoQueryIgnoresSenderAndValidQueryPropagatesSenderFailure)
{
	unsigned calls = 0;
	const auto sender = [&](int, QueryID)
	{
		++calls;
		throw std::runtime_error("Expected reply failure");
	};
	EXPECT_NO_THROW(sendBattleResultReply(0, QueryID::NONE, sender));
	EXPECT_EQ(calls, 0u);
	EXPECT_THROW(sendBattleResultReply(0, QueryID(17), sender), std::runtime_error);
	EXPECT_EQ(calls, 1u);
}
