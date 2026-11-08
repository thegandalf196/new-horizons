/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../json/JsonNode.h"
#include "../../constants/EntityIdentifiers.h"
#include "../../networkPacks/Component.h"
#include <vcmi/scripting/MapEventDispatcher.h>

class CGameState;
class CGTownInstance;
namespace scripting::nativeMap
{
struct Frame
{
	const JsonNode * actions = nullptr;
	size_t position = 0;
	PlayerColor player;
	std::string loopVariable;
	double loopNext = 0, loopEnd = -1;
};

struct Continuation
{
	ObjectInstanceID object, hero, town;
	PlayerColor originalPlayer;
	std::vector<Frame> frames;
	const JsonNode * pending = nullptr;
};

class Runtime final : public MapEventDispatcher
{
	CGameState & state;
	JsonNode program;
	std::map<int, Continuation> continuations;
	int nextHandle = 0;

	std::optional<int> begin(IGameEventCallback & server, const std::string & handler, Continuation context);
	bool step(IGameEventCallback & server, Continuation & context);
	bool action(IGameEventCallback & server, Continuation & context, const JsonNode & node);
	void reward(IGameEventCallback & server, Continuation & context, const JsonNode & node);
	void quest(IGameEventCallback & server, Continuation & context, const JsonNode & node);
	void combat(IGameEventCallback & server, Continuation & context, const JsonNode & node);
	void push(Continuation & context, const JsonNode & actions, PlayerColor player);
	void call(Continuation & context, const std::string & handler);
	PlayerColor player(const Continuation & context) const;
	PlayerColor resolvePlayer(const Continuation & context, const JsonNode & node) const;
	const CGHeroInstance & hero(const Continuation & context) const;
	const CGTownInstance & town(const Continuation & context) const;
	const CGObjectInstance & subject(const Continuation & context, const JsonNode & node) const;
	const CGObjectInstance * referencedObject(const JsonNode & node) const;
	std::string variable(const JsonNode & node) const;
	double expression(IGameEventCallback & server, const Continuation & context, const JsonNode & node) const;
	bool condition(IGameEventCallback & server, const Continuation & context, const JsonNode & node) const;
	std::vector<Component> components(IGameEventCallback & server, const Continuation & context, const JsonNode & images) const;
	void setVariable(IGameEventCallback & server, const std::string & name, double value) const;
	void message(IGameEventCallback & server, PlayerColor player, const std::string & text, const std::vector<Component> & components) const;
	void dialog(IGameEventCallback & server, PlayerColor player, const std::string & text, bool yesNo, const std::vector<Component> & components) const;

public:
	explicit Runtime(CGameState & state, JsonNode program, bool runInit);
	std::optional<int> onObjectVisit(IGameEventCallback &, const std::string &, const CGObjectInstance *, const CGHeroInstance *) override;
	std::optional<int> onPlayerTurnStart(IGameEventCallback &, const std::string &, PlayerColor) override;
	std::optional<int> onTownTurnStart(IGameEventCallback &, const std::string &, const CGTownInstance *) override;
	bool resumeCoroutine(IGameEventCallback &, int, std::optional<int>) override;
};
}
