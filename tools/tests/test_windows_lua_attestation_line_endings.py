"""Exact Lua capability bytes must survive native Windows Git checkout."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
# Roots are named by digest in the owner, rather than by resource path.
# A new digest/dispatch must extend this checked inventory explicitly.
ROOT_IDENTITIES = {
    'DAMAGE': 'spells/damage',
    'MARKED_DAMAGE': 'spells/damage',
    'LIFE_DRAIN': 'spells/lifeDrain',
    'SHARED_LIFE_DRAIN': 'spells/lifeDrain',
    'CURRENT_LIFE_DRAIN': 'spells/lifeDrain',
    'TIMED': 'spells/timed',
    'SHARED_TIMED': 'spells/timed',
    'MARKED_TIMED': 'spells/timed',
    'FOCUS_MAGIC': 'combat/focusMagic',
    'HEX_OF_PAIN': 'combat/hexOfPain',
}
IMPORT_IDENTITIES = {
    'DAMAGE': 'spells/damage',
    'UNIT_EFFECT': 'spells/unitEffect',
    'BATTLE_LOG': 'battleLog',
    'COMBAT_SCRIPT': 'combat/combatScript',
    'SORCERY_REVISION': 'spells/sorceryRevision',
    'SHARED_SORCERY_REVISION': 'spells/sorceryRevision',
}
UNKNOWN = 'scripts/unattested-line-ending-probe.lua'


def attested_paths():
    source = (ROOT / 'luascript/LuaImplementationIdentity.cpp').read_text()
    roots = set(re.findall(r'rootDigest == ([A-Z_]+)_SHA256', source))
    constants = set(re.findall(r'constexpr std::string_view ([A-Z_]+)_SHA256\s*=', source))
    if roots != set(ROOT_IDENTITIES) or constants != set(ROOT_IDENTITIES) | set(IMPORT_IDENTITIES):
        raise AssertionError('Attestation identity inventory changed; review its exact script paths')
    imports = set(re.findall(r'if\(name == "([^"]+)"\)', source))
    if imports != set(IMPORT_IDENTITIES.values()):
        raise AssertionError('Attestation import closure changed; review its exact script paths')
    # Includes every literal root/patch path in the reviewed layer tables.
    layers = set(re.findall(r'"((?:spells|damage|combat)/[A-Za-z0-9_/]+)"', source))
    names = layers | imports | set(ROOT_IDENTITIES.values())
    return sorted('scripts/' + name + '.lua' for name in names)


def reviewed_hashes_by_path():
    # Read only the owner's literal declarations; this is not an interpreter or closure proof.
    source = (ROOT / 'luascript/LuaImplementationIdentity.cpp').read_text()
    declarations = re.findall(
        r'^constexpr std::string_view ([A-Z_]+)_SHA256\s*=\s*"([0-9a-f]{64})";', source, re.M)
    constants = dict(declarations)
    identities = {**ROOT_IDENTITIES, **IMPORT_IDENTITIES}
    if len(constants) != len(declarations) or set(constants) != set(identities):
        raise AssertionError('Review the literal digest declarations before updating this drift alarm')
    accepted = {}
    for identity, path in identities.items():
        accepted.setdefault('scripts/' + path + '.lua', set()).add(constants[identity])
    layers = re.findall(
        r'\{\s*"((?:spells|damage|combat)/[A-Za-z0-9_/]+)"\s*,\s*("[0-9a-f]{64}"|[A-Z_]+_SHA256)\s*\}',
        source)
    for path, value in layers:
        digest = value[1:-1] if value.startswith('"') else constants[value.removesuffix('_SHA256')]
        accepted.setdefault('scripts/' + path + '.lua', set()).add(digest)
    if set(accepted) != set(attested_paths()):
        raise AssertionError('Every attested path must have an explicit reviewed digest')
    return accepted


def drifted_shipped_scripts():
    return sorted(path for path, accepted in reviewed_hashes_by_path().items()
                  if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() not in accepted)


def git_environment():
    # Git context variables override -C; isolate native Windows' case-insensitive environment too.
    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith('GIT_')}
    environment.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull, GIT_ATTR_NOSYSTEM='1')
    return environment


class LuaAttestationLineEndingsTest(unittest.TestCase):
    def setUp(self):
        self.environment = git_environment()
        self.git = shutil.which('git')
        self.assertIsNotNone(self.git, 'Git is required for the checkout contract')
        self.paths = attested_paths()
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name) / 'repo'
        self.repo.mkdir()
        self.command('init', '-q')
        self.command('config', 'core.autocrlf', 'false')
        (self.repo / '.gitattributes').write_bytes((ROOT / '.gitattributes').read_bytes())

    def command(self, *args):
        return subprocess.check_output([self.git, '-C', str(self.repo), *args],
                                       stderr=subprocess.PIPE, env=self.environment)

    def attributes(self, paths):
        fields = self.command('check-attr', '-z', 'text', 'eol', '--', *paths).decode().split('\0')
        self.assertEqual(fields.pop(), '')
        result = {}
        for offset in range(0, len(fields), 3):
            name, attribute, value = fields[offset:offset + 3]
            result.setdefault(name, {})[attribute] = value
        return result

    def test_every_attested_root_import_and_patch_has_explicit_lf(self):
        values = self.attributes(self.paths)
        for name in self.paths:
            with self.subTest(script=name):
                self.assertTrue((ROOT / name).is_file())
                self.assertEqual(values[name], {'text': 'set', 'eol': 'lf'})

    def test_windows_style_checkout_preserves_every_attested_digest(self):
        payloads = {name: (ROOT / name).read_bytes() for name in self.paths}
        payloads[UNKNOWN] = b'return { deliberatelyUnattested = true }\n'
        for name, raw in payloads.items():
            self.assertNotIn(b'\r', raw, name)
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        self.command('add', '--', '.gitattributes', *payloads)
        self.command('config', 'core.autocrlf', 'true')
        self.command('config', 'core.eol', 'crlf')
        checkout = self.repo.parent / 'checkout'
        self.command('checkout-index', '--all', '--force', '--prefix=' + checkout.as_posix() + '/')
        for name in self.paths:
            with self.subTest(script=name):
                checked = (checkout / name).read_bytes()
                self.assertEqual(checked, payloads[name])
                self.assertEqual(hashlib.sha256(checked).digest(), hashlib.sha256(payloads[name]).digest())
        # Demonstrate that CRLF conversion was active, rather than testing an inert configuration.
        self.assertEqual((checkout / UNKNOWN).read_bytes(), payloads[UNKNOWN].replace(b'\n', b'\r\n'))

    def test_historical_calculator_fixture_lf_and_literals_survive_windows_style_checkout(self):
        name = 'test/scripting/HistoricalDamageCalculatorScript.h'
        self.assertEqual(self.attributes([name])[name], {'text': 'set', 'eol': 'lf'})
        raw = (ROOT / name).read_bytes()
        self.assertNotIn(b'\r', raw)
        expression = raw.decode().split('SOURCE = ', 1)[1].rsplit(';', 1)[0]
        tokens = list(re.finditer(r'R"NH_403\((.*?)\)NH_403"|" "', expression, re.S))
        self.assertTrue(tokens)
        decoded = []
        cursor = 0
        for token in tokens:
            self.assertFalse(expression[cursor:token.start()].strip())
            self.assertLessEqual(len(token.group(0).encode()), 8000)
            decoded.append(token.group(1) if token.group(1) is not None else ' ')
            cursor = token.end()
        self.assertFalse(expression[cursor:].strip())
        source = ''.join(decoded).encode()
        self.assertEqual(len(source.splitlines()), 770)
        self.assertEqual(hashlib.sha256(source).hexdigest(),
                         '403340e1d0794736971478d9e0cc58d2f3efa4e8056cfcd3b138406012de2058')
        probe = b'unattested checkout conversion probe\n'
        for path_name, data in ((name, raw), (UNKNOWN, probe)):
            path = self.repo / path_name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.command('add', '--', '.gitattributes', name, UNKNOWN)
        self.command('config', 'core.autocrlf', 'true')
        self.command('config', 'core.eol', 'crlf')
        checkout = self.repo.parent / 'fixture-checkout'
        self.command('checkout-index', '--all', '--force', '--prefix=' + checkout.as_posix() + '/')
        checked = (checkout / name).read_bytes()
        self.assertEqual(checked, raw)
        self.assertEqual(hashlib.sha256(checked).digest(), hashlib.sha256(raw).digest())
        self.assertEqual((checkout / UNKNOWN).read_bytes(), probe.replace(b'\n', b'\r\n'))

    def test_inherited_git_context_cannot_mutate_external_repository(self):
        external = self.repo.parent / 'external'
        external.mkdir()
        def outside(*args):
            return subprocess.check_output([self.git, '-C', str(external), *args],
                                           stderr=subprocess.PIPE, env=git_environment())
        outside('init', '-q')
        (external / 'sentinel').write_bytes(b'unchanged outside repository\n')
        outside('add', '--', 'sentinel')
        def snapshot():
            return {str(path.relative_to(external)): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in external.rglob('*') if path.is_file()}
        before = snapshot()
        poison = {
            'GIT_DIR': str(external / '.git'), 'gIt_WoRk_TrEe': str(external),
            'GIT_WORK_TREE': str(external), 'GIT_INDEX_FILE': str(external / '.git/index'),
            'GIT_OBJECT_DIRECTORY': str(external / '.git/objects'),
            'GIT_ALTERNATE_OBJECT_DIRECTORIES': str(external / '.git/objects'),
            'GIT_CONFIG_COUNT': '2', 'GIT_CONFIG_KEY_0': 'core.worktree',
            'GIT_CONFIG_VALUE_0': str(external), 'GIT_CONFIG_KEY_1': 'core.hooksPath',
            'GIT_CONFIG_VALUE_1': str(external / '.git/hooks'),
            'GIT_CONFIG_PARAMETERS': "'core.bare=false'", 'GIT_EXEC_PATH': str(external),
        }
        with patch.dict(os.environ, poison):
            self.environment = git_environment()
            self.assertEqual({key: value for key, value in self.environment.items()
                              if key.upper().startswith('GIT_')}, {
                                  'GIT_CONFIG_NOSYSTEM': '1', 'GIT_CONFIG_GLOBAL': os.devnull,
                                  'GIT_ATTR_NOSYSTEM': '1'})
            self.command('init', '-q')
            self.command('config', 'containment.check', 'local-only')
            self.command('add', '--', '.gitattributes')
            self.assertEqual(self.command('config', '--local', '--get', 'containment.check').strip(),
                             b'local-only')
        self.assertTrue((self.repo / '.git/index').is_file())
        self.assertEqual(snapshot(), before)

    def test_shipped_scripts_match_an_owner_reviewed_digest_for_their_path(self):
        self.assertEqual(drifted_shipped_scripts(), [])

    def test_byte_drift_and_another_paths_digest_are_rejected(self):
        owner = 'luascript/LuaImplementationIdentity.cpp'
        payloads = {name: (ROOT / name).read_bytes() for name in [*self.paths, owner]}
        for name, raw in payloads.items():
            target = self.repo / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
        changed = 'scripts/spells/timed.lua'
        for raw in (payloads[changed] + b'\n-- unreviewed source drift\n',
                    payloads['scripts/spells/damage.lua']):
            (self.repo / changed).write_bytes(raw)
            with patch.dict(globals(), ROOT=self.repo):
                self.assertEqual(drifted_shipped_scripts(), [changed])
        self.assertNotIn(UNKNOWN, reviewed_hashes_by_path())

    def test_unattested_scripts_are_not_blanket_normalized(self):
        self.assertNotIn(UNKNOWN, self.paths)
        self.assertEqual(self.attributes([UNKNOWN])[UNKNOWN], {'text': 'unspecified', 'eol': 'unspecified'})


if __name__ == '__main__':
    unittest.main()
