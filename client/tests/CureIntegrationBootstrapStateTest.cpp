/*
 * CureIntegrationBootstrapStateTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "CureIntegrationBootstrapState.h"

#include <iostream>

namespace
{
void require(bool value)
{
	if(!value)
		throw std::runtime_error("Cure bootstrap state regression");
}

template<typename Operation> void refuses(Operation operation)
{
	bool rejected = false;
	try
	{
		operation();
	}
	catch(const std::runtime_error &)
	{
		rejected = true;
	}
	require(rejected);
}

void absentInvalidAndOnce()
{
	cureBootstrap::State absent(nullptr);
	require(!absent.consume() && !absent.awaitingLoaded());
	for(const char * value : {"", "1", "Ordinary44b24", "ordinary44b24 ", "Maps/other.vmap"})
		refuses([value]() { cureBootstrap::State invalid(value); });
	cureBootstrap::State state("ordinary44b24");
	require(state.consume() && state.awaitingLoaded());
	int connections = 0;
	refuses([&connections]()
	{
		++connections;
		throw std::runtime_error("failed connection after request consumed");
	});
	for(int update = 0; update < 5; ++update)
		require(!state.consume()); // Failure, duplicate update and restart do not rearm.
	require(connections == 1 && state.awaitingLoaded());
	state.acceptLoaded();
	require(!state.awaitingLoaded() && !state.consume());
}

void fixedResourceAndHeader()
{
	cureBootstrap::validateResource(true, 1581, cureBootstrap::DIGEST);
	refuses([]() { cureBootstrap::validateResource(false, 1581, cureBootstrap::DIGEST); });
	refuses([]() { cureBootstrap::validateResource(true, 0, cureBootstrap::DIGEST); });
	refuses([]() { cureBootstrap::validateResource(true, 1581, "wrong-map"); });
	cureBootstrap::Header correct;
	correct.human[0] = true;
	correct.computer[0] = correct.computer[1] = true;
	correct.width = correct.height = 36;
	correct.normal = correct.ordinary = true;
	cureBootstrap::validateHeader(correct);
	for(std::size_t index = 0; index < 8; ++index)
	{
		auto header = correct;
		header.human[index] = !header.human[index];
		refuses([&header]() { cureBootstrap::validateHeader(header); });
		header = correct;
		header.computer[index] = !header.computer[index];
		refuses([&header]() { cureBootstrap::validateHeader(header); });
	}
	for(int wrong = 0; wrong < 4; ++wrong)
	{
		auto header = correct;
		if(wrong == 0) header.width = 72;
		if(wrong == 1) header.height = 72;
		if(wrong == 2) header.normal = false;
		if(wrong == 3) header.ordinary = false;
		refuses([&header]() { cureBootstrap::validateHeader(header); });
	}
}

void actualWorldContract()
{
	cureBootstrap::Loaded correct;
	correct.human[0] = correct.computer[1] = true;
	correct.exactMap = correct.newGame = correct.normal = true;
	correct.redTyris = correct.threeRedStacks = correct.neutralIce200 = true;
	cureBootstrap::validateLoaded(correct);
	for(std::size_t index = 0; index < 8; ++index)
	{
		auto loaded = correct;
		loaded.human[index] = !loaded.human[index];
		refuses([&loaded]() { cureBootstrap::validateLoaded(loaded); });
		loaded = correct;
		loaded.computer[index] = !loaded.computer[index];
		refuses([&loaded]() { cureBootstrap::validateLoaded(loaded); });
	}
	for(auto field : {&cureBootstrap::Loaded::exactMap, &cureBootstrap::Loaded::newGame,
		&cureBootstrap::Loaded::normal, &cureBootstrap::Loaded::redTyris,
		&cureBootstrap::Loaded::threeRedStacks, &cureBootstrap::Loaded::neutralIce200})
	{
		auto loaded = correct;
		loaded.*field = false;
		refuses([&loaded]() { cureBootstrap::validateLoaded(loaded); });
	}
}
}

int main()
{
	try
	{
		absentInvalidAndOnce();
		fixedResourceAndHeader();
		actualWorldContract();
		std::cout << "PASS pure Cure bootstrap state/contracts; not rendered acceptance\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
