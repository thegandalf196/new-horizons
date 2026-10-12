/*
 * WindowSceneRedrawTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../gui/WindowHandler.h"

#include <iostream>
#include <stdexcept>

struct Renderable
{
	int paints = 0;
	void showAll() { ++paints; }
};

static void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

int main()
{
	try
	{
		SceneRedrawState<Renderable> state;
		Renderable oldBattle;
		Renderable newScene;
		state.request(&oldBattle);
		state.request(&oldBattle);
		state.clearScene(); // Same scheduling operation used by WindowHandler::clear.
		require(state.fullRedrawRequested(), "Scene teardown must request a complete repaint");
		for(auto * object : state.takePending())
			object->showAll();
		if(state.fullRedrawRequested())
			newScene.showAll();
		state.finishFrame();
		require(oldBattle.paints == 0 && newScene.paints == 1,
			"Externally retained old battle must not repaint over the new scene");
		require(!state.fullRedrawRequested(), "Completed frame must consume full repaint request");

		state.request(&newScene);
		state.request(&newScene);
		auto pending = state.takePending();
		require(pending.size() == 1 && pending.front() == &newScene,
			"Ordinary deferred paints must retain order and deduplicate");
		pending.front()->showAll();
		require(newScene.paints == 2, "Ordinary deferred paints must still execute");
		require(state.takePending().empty(), "Consumed deferred paints must not repeat");

		state.request(&oldBattle);
		state.request(&newScene);
		state.cancel(&oldBattle);
		pending = state.takePending();
		require(pending.size() == 1 && pending.front() == &newScene,
			"Cancellation must remove only the specified retained object");
		state.cancel(&oldBattle); // Empty-queue fast path remains safe.
		state.clearScene();
		state.clearScene();
		require(state.takePending().empty() && state.fullRedrawRequested(),
			"Repeated teardown must remain empty and request full repaint");
		std::cout << "Window scene redraw lifecycle: all checks passed\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
