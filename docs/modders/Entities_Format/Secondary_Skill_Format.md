# Secondary Skill Format

## Main format

```json
{
	"skillName":
	{
		//Mandatory, localizable skill name
		"name":     "Localizable name",

		// Optional base format, will be merged with basic/advanced/expert
		"base":     {Skill level base format},

		// Configuration for different skill levels
		"basic":    {Skill level format},
		"advanced": {Skill level format},
		"expert":   {Skill level format},
		
		// Names of bonuses of the skill that are affected by default secondary skill specialty of a hero
		"specialty" : [
			"main"
		],
		
		// Chance for the skill to be offered on level-up (heroClass may override)
		/// Identifier without modID specifier MUST exist in base game or in one of dependencies
		/// Identifier with explicit modID specifier will be silently skipped if corresponding mod is not loaded
		"gainChance" : {
			// Chance for hero classes with might affinity
			"might" : 4,
			// Chance for hero classes with magic affinity
			"magic" : 6,
			// Chance for specific classes
			"knight" : 2,
			"cleric" : 8,
			...
			"modName:heroClassName" : 5
		},
		
        // List of tags that describe this skill
		// Tags are only active if set to 'true'
		// All values are optional
		// It is also possible to define own tag and test for it when rolling for skills
		"tags" : {
		    // Skill is banned on random maps without water
			"onlyOnWaterMap" : false,
			// Skill is considered to be disabled on maps by default
			"special" : true,
			// This skill is guaranteed to be offered once per specific number of levels
			// according to H3 logic for Wisdom
			"wisdom" : false,
			// This skill is guaranteed to be offered once per specific number of levels
			// according to H3 logic for Spell Schools
			"spellSchool" : true
		}
	}
}
```

## Optional combat status

A skill can opt into a read-only combat status entry using a top-level
`combatStatus` object:

```json
"combatStatus": {
	"provider": "metamagicUses",
	"description": "Remaining uses for this combat. A use is spent only when an additional spell is cast."
}
```

Bloodrage uses the same metadata shape:

```json
"combatStatus": {
	"provider": "bloodrageDamage",
	"description": "Current cumulative creature damage bonus from Bloodrage, up to the cap for the learned rank."
}
```

`provider` selects an implemented status reader, not a script or arbitrary
expression. `divineMandateUses` reads the remaining/maximum completed Divine
Mandate pairs and any pending Light-Spell-only or Order-only opportunity from
saved battle state. Unused opportunities expire at round end without spending a
pair; a completed follow-up spends one pair. It is not a separate action pool.
`metamagicUses` reads the authoritative remaining/maximum Metamagic
combat allowance. `bloodrageDamage` reads the current cumulative Bloodrage damage
bonus and the cap for its learned rank from battle state. Its compact value is
shown as `+current/cap%`; the tooltip supplies the expanded explanation. Declaring a provider
does not grant the skill, actions, or additional uses. Unknown providers are
invalid.

`description` is localizable tooltip text. The entry reuses the skill's translated
name and learned rank's small icon. Skills without this object produce no entry
and reserve no space. Hero, Spell, and Order Actions remain separate round-scoped
allowances; this status must not be interpreted as an extra action pool. Future
mechanics can add their own typed providers without adding dedicated fields to
the status widget.

## Skill level base format

Json object with data common for all levels can be put here. These
configuration parameters will be default for all levels. All mandatory
level fields become optional if they equal "base" configuration.

## Skill level format

```json
{
	// Localizable description
	// Use {xxx} for formatting
	"description": "",

	// Bonuses provided by skill at given level
	// If different levels provide same bonus with different val, only the highest applies
	"effects":
	{
		"firstEffect":  {bonus format},
		"secondEffect": {bonus format}
		//...
	},
	
	// Skill icons of varying size
	"images" : {
		// 32x32 skill icon
		"small" : "",
		// 44x44 skill icon
		"medium" : "",
		// 82x93 skill icon
		"large" : "",
		// 58x64 skill icon for campaign scenario bonus
		"scenarioBonus" : ""
	}
}
```

## Example

The following modifies the tactics skill to grant an additional speed
boost at advanced and expert levels.

```json
"core:tactics" : {
	"base" : {
		"effects" : {
			"main" : {
				"subtype" : "skill.tactics",
				"type" : "SECONDARY_SKILL_PREMY",
				"valueType" : "BASE_NUMBER"
			},
			"xtra" : {
				"type" : "STACKS_SPEED",
				"valueType" : "BASE_NUMBER"
			}
		}
	},
	"basic" : {
		"effects" : {
			"main" : { "val" : 3 },
			"xtra" : { "val" : 0 }
		}
	},
	"advanced" : {
		"description" : "{Advanced Tactics}\n\nAllows you to rearrange troups within 5 hex rows, and increases their speed by 1.",
		"effects" : {
			"main" : { "val" : 5 },
			"xtra" : { "val" : 1 }
		}
	},
	"expert" : {
		"description" : "{Expert Tactics}\n\nAllows you to rearrange troups within 7 hex rows, and increases their speed by 2.",
		"effects" : {
			"main" : { "val" : 7 },
			"xtra" : { "val" : 2 }
		}
	}
}
```
