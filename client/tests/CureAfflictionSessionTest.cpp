/*
 * Standalone Cure modal session regression; no engine/SDL/assets required.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../battle/CureAfflictionSession.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void require(bool value)
{
	if(!value)
		throw std::runtime_error("Cure modal session regression");
}

struct Controller
{
	uint64_t generation = 7;
	bool casting = true;
	std::vector<std::string> * events;

	uint64_t getCastingSession() const { return generation; }
	bool heroSpellcastingModeActive() const { return casting; }
	void endCastingSpell()
	{
		events->push_back("end");
		++generation;
		casting = false;
	}
};

struct Battle
{
	std::vector<std::string> events;
	std::unique_ptr<Controller> actionsController = std::make_unique<Controller>();
	bool turn = true;
	int hero = 11;
	int battleID = 17;

	Battle() { actionsController->events = &events; }
	bool makingTurn() const { return turn; }
};

void validSessionAndRepeatedCallbacks()
{
	auto owner = std::make_shared<Battle>();
	const CureAfflictionSession session(owner, owner->actionsController->getCastingSession());
	require(owner.use_count() == 1); // Session owns no persistent strong battle reference.
	auto submit = [](Battle & battle)
	{
		require(!battle.actionsController->heroSpellcastingModeActive());
		battle.events.push_back("submit");
	};
	session.confirm(owner, submit);
	require(owner->events == std::vector<std::string>{"end", "submit"});
	require(owner.use_count() == 1);
	session.confirm(owner, submit);
	session.cancel(owner);
	require(owner->events == std::vector<std::string>{"end", "submit"});
}

void staleGenerationModeAndTurn()
{
	for(int scenario = 0; scenario != 4; ++scenario)
	{
		auto owner = std::make_shared<Battle>();
		const CureAfflictionSession session(owner, 7);
		if(scenario == 0)
			++owner->actionsController->generation;
		else if(scenario == 1)
			owner->actionsController->casting = false;
		else if(scenario == 2)
			owner->turn = false;
		else
			owner->actionsController.reset();
		session.confirm(owner, [](Battle &) { require(false); });
		require(owner->events.empty());
	}
}

void replacedAndDetachedBattleBeforeDestruction()
{
	auto old = std::make_shared<Battle>();
	const CureAfflictionSession session(old, 7);
	auto replacement = std::make_shared<Battle>();
	// IDs and generation may repeat after restart; weak owner identity may not.
	require(old->hero == replacement->hero && old->battleID == replacement->battleID);
	session.confirm(replacement, [](Battle &) { require(false); });
	session.cancel(replacement);
	require(old->events.empty() && replacement->events.empty());
	// CClient::endGame detaches global battle before clear, while still retaining it.
	session.confirm({}, [](Battle &) { require(false); });
	session.cancel({});
	require(old->events.empty());
	old.reset();
	session.confirm(replacement, [](Battle &) { require(false); });
	session.cancel(replacement);
	require(replacement->events.empty());
}

void closeAndRestartCannotCancelNewCast()
{
	auto owner = std::make_shared<Battle>();
	const CureAfflictionSession old(owner, 7);
	old.cancel(owner);
	require(owner->events == std::vector<std::string>{"end"});
	owner->actionsController->casting = true;
	++owner->actionsController->generation;
	old.cancel(owner);
	old.confirm(owner, [](Battle &) { require(false); });
	require(owner->events == std::vector<std::string>{"end"});
	require(owner->actionsController->casting);
	const CureAfflictionSession fresh(owner, owner->actionsController->getCastingSession());
	fresh.confirm(owner, [](Battle & battle) { battle.events.push_back("fresh"); });
	require(owner->events == std::vector<std::string>{"end", "end", "fresh"});
}

void sameSessionHeroChangePreservesRevalidationOrder()
{
	auto owner = std::make_shared<Battle>();
	const CureAfflictionSession session(owner, 7);
	const int expectedHero = owner->hero;
	owner->hero = 12;
	// This continuation is a spy for the concrete installer's live-state phase,
	// not a test of engine Hero/target lookup or spell legality. The adapter must
	// still end the same targeting session before invoking that phase.
	session.confirm(owner, [expectedHero](Battle & battle)
	{
		battle.events.push_back("revalidate");
		if(battle.hero != expectedHero)
			return;
		battle.events.push_back("submit");
	});
	require(owner->events == std::vector<std::string>{"end", "revalidate"});
}

void cancellationKeepsOriginalGuardScope()
{
	auto owner = std::make_shared<Battle>();
	const CureAfflictionSession session(owner, 7);
	owner->turn = false;
	owner->actionsController->casting = false;
	// Original onExit checks only the captured generation, unlike confirmation.
	session.cancel(owner);
	require(owner->events == std::vector<std::string>{"end"});
}
}

int main()
{
	validSessionAndRepeatedCallbacks();
	staleGenerationModeAndTurn();
	replacedAndDetachedBattleBeforeDestruction();
	closeAndRestartCannotCancelNewCast();
	sameSessionHeroChangePreservesRevalidationOrder();
	cancellationKeepsOriginalGuardScope();
	return 0;
}
