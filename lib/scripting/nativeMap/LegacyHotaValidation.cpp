/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "LegacyHotaImport.h"
#include "../../constants/EntityIdentifiers.h"

namespace scripting::nativeMap::legacy
{
void requireCall(const JsonNode & value, const std::string & name, size_t arguments, bool tableCall)
{
	if(value["kind"].String() != "call" || value["text"].String() != name
		|| value["args"].Vector().size() != arguments || value["tableCall"].Bool() != tableCall)
		unsupported("call signature " + name);
}
void requireName(const JsonNode & value, const std::string & name)
{
	if(value["kind"].String() != "name" || value["text"].String() != name) unsupported("expected symbol " + name);
}
void requireArray(const JsonNode & value, std::optional<size_t> size)
{
	if(value["kind"].String() != "table" || !value["fields"].Struct().empty()
		|| (size && value["args"].Vector().size() != *size)) unsupported("array table shape");
}
void requireRecord(const JsonNode & value, const std::vector<std::string> & required, const std::vector<std::string> & optional)
{
	if(value["kind"].String() != "table" || !value["args"].Vector().empty()) unsupported("record table shape");
	for(const auto & key : required) if(value["fields"][key].isNull()) unsupported("missing table field " + key);
	for(const auto & entry : value["fields"].Struct())
		if(std::find(required.begin(), required.end(), entry.first) == required.end()
			&& std::find(optional.begin(), optional.end(), entry.first) == optional.end()) unsupported("unexpected table field " + entry.first);
}
bool booleanLiteral(const JsonNode & value)
{
	const auto & name = value["text"].String();
	if(value["kind"].String() != "name" || (name != "true" && name != "false")) unsupported("expected boolean literal");
	return name == "true";
}
int enumLiteral(const JsonNode & value, const std::string & group)
{
	const auto & name = value["text"].String();
	const auto prefix = "ENUM." + group + ".";
	if(value["kind"].String() != "name" || name.compare(0, prefix.size(), prefix) != 0) unsupported("expected " + group + " enum");
	const auto key = name.substr(prefix.size());
	if(group == "PrimarySkill")
	{
		for(int id = 0; id < 4; ++id) if(PrimarySkill::encode(id) == key) return id;
		unsupported("unknown primary skill enum");
	}
	const auto id = PlayerColor::decode(key);
	if(id < -1 || id >= PlayerColor::PLAYER_LIMIT_I || PlayerColor::encode(id) != key) unsupported("unknown player enum");
	return id;
}
std::string handlerBucket(const std::string & name)
{
	const auto separator = name.find('_');
	if(separator == std::string::npos) unsupported("handler name " + name);
	const auto bucket = name.substr(0, separator), id = name.substr(separator + 1);
	if((bucket != "heroEvents" && bucket != "questEvents" && bucket != "townEvents" && bucket != "playerEvents")
		|| id.empty() || id.find_first_not_of("0123456789") != std::string::npos) unsupported("handler name " + name);
	return bucket;
}
void validateActionCall(const JsonNode & value, const std::string & bucket)
{
	const auto & name = value["text"].String();
	const auto & args = value["args"].Vector();
	const auto subject = bucket == "townEvents" ? "town" : "object";
	if(name.compare(0, 4, "Map:") == 0)
	{
		const auto target = handlerBucket(name.substr(4));
		const auto category = [](const auto & bucket) { return bucket == "townEvents" ? 2 : bucket == "playerEvents" ? 1 : 0; };
		if(category(bucket) != category(target)) unsupported("event call uses incompatible bucket");
		requireCall(value, name, category(bucket) == 0 ? 4 : 3);
		requireName(args[0], "game"); requireName(args[1], "server");
		requireName(args[2], category(bucket) == 0 ? "object" : category(bucket) == 1 ? "player" : "town");
		if(category(bucket) == 0) requireName(args[3], "hero");
		return;
	}
	const std::map<std::string, size_t> calls{{"server:setMapVariable",2},{"server:removeObject",1},{"server:finishQuestOrRemoveObject",1},
		{"server:showMessage",3},{"server:showRewardsMessage",3},{"server:showQuestion",1},{"registerQuest",5},{"setQuestHint",6},
		{"grantResources",3},{"server:startCombat",2},{"server:takeArtifact",2},{"server:grantArtifact",2},{"server:grantScroll",2},
		{"server:takeWarMachine",2},{"server:grantWarMachine",2},{"server:takeSpellbook",1},{"server:grantSpellbook",1},
		{"server:grantCreatures",3},{"server:takeCreatures",3},{"server:grantSpell",2},{"server:giveExperience",2},
		{"server:grantPrimarySkill",3},{"server:grantSecondarySkill",3},{"server:grantLuck",2},{"server:grantMorale",2},
		{"server:grantSpellPoints",3},{"server:grantMovementPoints",3},{"server:grantCreaturesToHire",3},{"server:constructBuilding",2}};
	const auto found = calls.find(name);
	if(found == calls.end()) unsupported("action call " + name);
	requireCall(value, name, found->second, name == "server:showQuestion");
	if(name == "server:setMapVariable" || name == "server:showQuestion") return;
	if(name == "server:removeObject" || name == "server:finishQuestOrRemoveObject")
	{
		if(name == "server:finishQuestOrRemoveObject" && bucket != "questEvents") unsupported("quest completion bucket");
		requireName(args[0], subject); return;
	}
	if(name == "server:showMessage" || name == "server:showRewardsMessage") { requireName(args[0], "player"); return; }
	if(name == "registerQuest")
	{
		requireName(args[0], "game"); requireName(args[1], "server"); requireName(args[2], "player"); requireName(args[3], subject); return;
	}
	if(name == "setQuestHint" || name == "grantResources")
	{
		requireName(args[0], "server"); requireName(args[1], "player");
		if(name == "setQuestHint") requireName(args[2], subject);
		return;
	}
	requireName(args[0], name == "server:grantCreaturesToHire" || name == "server:constructBuilding" ? "town" : "hero");
}
}
