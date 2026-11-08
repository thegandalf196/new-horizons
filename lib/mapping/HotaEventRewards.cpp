/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HotaScriptConverter.h"
#include "MapReaderH3M.h"
#include "../scripting/nativeMap/EventCodes.h"

void HotaScriptConverter::loadRewardAction(int raw, JsonNode & node)
{
	switch(static_cast<HotaScriptActions>(raw))
	{
		case HotaScriptActions::ARTIFACT:
			node["take"].Bool() = reader.readBool();
			node["entity"].String() = ArtifactID::encode(reader.readArtifact32().getNum());
			{ const auto spell = reader.readSpell32(); if(spell.getNum() >= 0) node["scroll"].String() = SpellID::encode(spell.getNum()); }
			reader.readBool(); break;
		case HotaScriptActions::WAR_MACHINE:
			node["take"].Bool() = reader.readBool();
			node["entity"].String() = ArtifactID::encode(reader.readArtifact32().getNum());
			reader.skipUnused(4); reader.readBool(); break;
		case HotaScriptActions::SPELL:
			node["entity"].String() = SpellID::encode(reader.readSpell32().getNum()); reader.readBool(); break;
		case HotaScriptActions::SPELLBOOK:
			node["take"].Bool() = reader.readBool(); reader.skipUnused(8); reader.readBool(); break;
		case HotaScriptActions::CREATURES:
			node["take"].Bool() = reader.readBool();
			node["entity"].String() = CreatureID::encode(reader.readCreature32().getNum());
			node["amount"] = loadExpression(); reader.readBool(); break;
		case HotaScriptActions::START_COMBAT:
			for(int i = 0; i < 7; ++i)
			{
				JsonNode slot;
				slot["amount"] = loadExpression();
				slot["entity"].String() = CreatureID::encode(reader.readCreature32().getNum());
				node["army"].Vector().push_back(std::move(slot));
			}
			break;
		case HotaScriptActions::SECONDARY_SKILL:
			node["mastery"].Integer() = reader.readInt32();
			node["entity"].String() = SecondarySkill::encode(reader.readSkill32().getNum()); reader.readBool(); break;
		case HotaScriptActions::MORALE: case HotaScriptActions::LUCK:
			node["amount"].Integer() = reader.readInt32(); reader.readBool(); break;
		case HotaScriptActions::EXPERIENCE:
			node["amount"] = loadExpression(); reader.readBool(); break;
		case HotaScriptActions::SPELL_POINTS: case HotaScriptActions::MOVEMENT_POINTS:
			node["amount"] = loadExpression(); node["mode"].Integer() = reader.readInt32(); reader.readBool(); break;
		case HotaScriptActions::CREATURES_TO_HIRE:
			node["level"].Integer() = reader.readUInt32(); node["amount"] = loadExpression();
			{ const int unknown = reader.readInt32(); reader.readBool(); if(unknown != -1) throw unsupported("CREATURES_TO_HIRE with unknown field"); }
			break;
		case HotaScriptActions::CONSTRUCT_BUILDING:
		{
			node["building"].Integer() = reader.readBuilding32(std::nullopt).getNum();
			const int a = reader.readInt16(); const int b = reader.readInt16(); reader.readBool();
			if(a != -1 || b != -1) throw unsupported("CONSTRUCT_BUILDING with faction fields");
			break;
		}
		case HotaScriptActions::RESOURCES:
		{
			const int mode = reader.readInt8();
			for(int i = 0; i < 7; ++i) node["amounts"].Vector().push_back(loadExpression());
			reader.readBool(); if(mode != 0) throw unsupported("RESOURCES with unsupported mode");
			break;
		}
		case HotaScriptActions::PRIMARY_SKILL:
			node["amount"] = loadExpression(); node["skill"].Integer() = reader.readInt32(); reader.readBool(); break;
		default: throw unsupported("Unknown event action code " + std::to_string(raw));
	}
}
