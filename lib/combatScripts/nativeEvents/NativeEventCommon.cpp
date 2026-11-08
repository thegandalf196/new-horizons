/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEventCommon.h"
#include "../../GameLibrary.h"
#include "../../networkPacks/PacksForClientBattle.h"
#include "../../spells/CSpell.h"
#include "../../texts/MetaString.h"
#include <vcmi/CreatureService.h>
#include <vcmi/spells/Service.h>

namespace nativeEvents
{
bool truthy(const JsonNode & value)
{
	return !value.isNull() && !(value.isBool() && !value.Bool());
}

double number(const JsonNode & parameters, const std::string & key, double fallback)
{
	const auto & value = parameters[key];
	if(!truthy(value)) return fallback;
	if(!value.isNumber()) throw std::runtime_error("Combat event parameter is not a number: " + key);
	return value.Float();
}

std::string string(const JsonNode & parameters, const std::string & key, const std::string & fallback)
{
	return truthy(parameters[key]) ? parameters[key].String() : fallback;
}

const CSpell * spell(const std::string & name)
{
	return dynamic_cast<const CSpell *>(LIBRARY->spells()->getByName(name));
}

const Creature * creature(const std::string & name)
{
	return LIBRARY->creatures()->getByName(name);
}

std::string creatureName(const battle::Unit * unit, int count)
{
	if(!unit) return count == 1 ? "core.genrltxt.42" : "core.genrltxt.43";
	const auto * type = unit->creatureId().toEntity(LIBRARY);
	return count == 1 ? type->getNameSingularTextID() : type->getNamePluralTextID();
}

void log(const Context & context, const MetaString & message)
{
	BattleLogMessage pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	pack.lines.push_back(message);
	context.server.apply(pack);
}

void perish(const Context & context, const battle::Unit * victim, int64_t killed)
{
	if(killed <= 0) return;
	MetaString message;
	message.appendTextID(killed == 1 ? "core.genrltxt.378" : "core.genrltxt.379");
	message.replaceTextID(creatureName(victim, static_cast<int>(killed)));
	message.replaceNumber(killed);
	log(context, message);
}

void spellDamageLog(const Context & context, const CSpell & source, const battle::Unit * victim, int64_t damage, int64_t killed)
{
	MetaString message;
	message.appendTextID("core.genrltxt.376");
	message.replaceTextID(source.getNameTextID());
	message.replaceNumber(damage);
	log(context, message);
	perish(context, victim, killed);
}

void lifeDrainedLog(const Context & context, int64_t healed, int64_t resurrected, int originalCount)
{
	MetaString message;
	message.appendTextID(originalCount == 1 ? "core.genrltxt.361" : "core.genrltxt.362");
	if(resurrected == 1) message.appendTextID("core.genrltxt.363");
	else if(resurrected > 1) message.appendTextID("core.genrltxt.364");
	message.replaceTextID(creatureName(context.self, originalCount));
	message.replaceTextID(creatureName(context.other, context.other ? context.other->getCount() : 0));
	message.replaceNumber(healed);
	if(resurrected > 1) message.replaceNumber(resurrected);
	log(context, message);
}
}
