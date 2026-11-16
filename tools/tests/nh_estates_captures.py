"""Public pre-revision Estates fixture shared by data and native compatibility checks."""
import copy
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def legacy_estates_skill():
    header = (ROOT / 'test/server/LegacyEstatesCapture.h').read_text(encoding='utf-8')
    return json.loads(re.search(r'R"estates\((.*?)\)estates"', header, re.S).group(1))


def legacy_estates_perks(current):
    rules = copy.deepcopy(current)
    rules.pop('estatesPolicyVersion', None)
    rules.pop('fourOfferProgressionVersion', None)
    rules['unifiedSkillProgressionVersion'] = 1
    rules['skills']['new-horizons:estates'] = legacy_estates_skill()
    return rules
