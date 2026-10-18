/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../lib/StdInc.h"
#include "SaveParityInput.h"
#include "SaveParityEvidence.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/modding/CModHandler.h"
#include <algorithm>
#include <iostream>

namespace
{
struct Inspected
{
	saveParity::Evidence evidence;
	ESerializationVersion version;
};

Inspected inspect(const boost::filesystem::path & path)
{
	auto first = saveParity::readSave(path);
	Inspected result{saveParity::capture(*first->handler), first->version};
	auto repeated = saveParity::capture(*first->handler);
	if(!saveParity::firstDifference(result.evidence, repeated).empty()
		|| result.evidence.rawStatistics != repeated.rawStatistics)
		throw std::runtime_error("Repeated detached capture is not stable");
	first.reset();
	auto independent = saveParity::readSave(path);
	auto second = saveParity::capture(*independent->handler);
	if(result.version != independent->version
		|| !saveParity::firstDifference(result.evidence, second).empty()
		|| result.evidence.rawStatistics != second.rawStatistics)
		throw std::runtime_error("Independent native save decodes are not stable");
	return result;
}

void requireShippingMods()
{
	const auto & mods = LIBRARY->modh->getActiveMods();
	if(std::find(mods.begin(), mods.end(), "vcmi") == mods.end()
		|| std::find(mods.begin(), mods.end(), "new-horizons") == mods.end()
		|| std::any_of(mods.begin(), mods.end(), [](const auto & mod)
		{ return mod != "core" && mod != "vcmi" && mod != "new-horizons"; }))
		throw std::runtime_error("Save inspection requires the ordinary shipping mod preset");
}

void report(const Inspected & start, const Inspected & left, const Inspected & right)
{
	const auto difference = saveParity::firstDifference(left.evidence, right.evidence);
	std::cout << "{\"schema\":1,\"equal\":" << (difference.empty() ? "true" : "false")
		<< ",\"first_difference\":\"" << difference << "\",\"normalization\":\"statistics.timestamp only\","
		<< "\"scope\":\"native saved representation and recorded effects; not all internal choices\","
		<< "\"formats\":[" << static_cast<int>(start.version) << ',' << static_cast<int>(left.version)
		<< ',' << static_cast<int>(right.version) << "],\"channels\":{";
	bool comma = false;
	for(const auto & [name, data] : left.evidence.channels)
	{
		if(comma)
			std::cout << ',';
		comma = true;
		std::cout << '"' << name << "\":[" << data.size() << ',' << right.evidence.channels.at(name).size() << ']';
	}
	std::cout << "}}\n";
}
}

int main(int argc, char ** argv)
{
	if(argc != 4)
	{
		std::cerr << "Usage: nhSaveParity START.vsgm1 LEFT.vsgm1 RIGHT.vsgm1\n";
		return 2;
	}
	try
	{
		NativeBonusGraphAccess graphAccess;
		auto library = std::make_unique<GameLibrary>();
		LIBRARY = library.get();
		library->initializeFilesystem(false);
		requireShippingMods();
		library->initializeLibrary();
		const auto start = inspect(argv[1]);
		const auto left = inspect(argv[2]);
		const auto right = inspect(argv[3]);
		if(left.version != right.version)
			throw std::runtime_error("Compared final saves use different native formats");
		saveParity::requireTurnCoverage(start.evidence, left.evidence);
		saveParity::requireTurnCoverage(start.evidence, right.evidence);
		report(start, left, right);
		return saveParity::firstDifference(left.evidence, right.evidence).empty() ? 0 : 1;
	}
	catch(const std::exception & error)
	{
		std::cerr << "Save inspection refused: " << error.what() << '\n';
		return 2;
	}
}
