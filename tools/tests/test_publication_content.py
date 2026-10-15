#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic R1/R2 policy and exact Git-object selection regression tests."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))

import git_publication_blobs as blobs
import publication_content as policy


def transcript():
    return '\n'.join(['<|' + 'im_start|>user', 'SYNTHETIC body only', '<|' + 'im_end|>',
                      '<|' + 'im_start|>assistant', 'SYNTHETIC answer only', '<|' + 'im_end|>'])


def session():
    return {'session_id': 'synthetic-session',
            'messages': [{'role': 'user', 'content': 'SYNTHETIC body only'}]}


class Detectors(unittest.TestCase):
    def test_actual_raw_role_frames_not_lone_markers(self):
        self.assertEqual(policy.find_content_frames(transcript()), [{'rule': 'raw-role-envelope', 'line': 1}])
        for text in ['User: normal UI documentation\nAssistant: example',
                     '<|' + 'im_start|>user\nno closing',
                     json.dumps({'example': transcript()}),
                     '<|' + 'im_start|>user\nfirst\n<|' + 'im_end|>']:
            self.assertEqual(policy.find_content_frames(text), [])

    def test_conversation_boundaries_roles_and_bodies_required(self):
        text = '\n'.join(['<<< BEGIN OF CONVERSATION >>>', '<<< user >>>', 'synthetic',
                          '<<< assistant >>>', 'synthetic answer', '<<< END OF CONVERSATION >>>'])
        self.assertEqual(policy.frame_lines(text), [1])
        self.assertEqual(policy.frame_lines(text.replace('<<< END OF CONVERSATION >>>', '')), [])
        self.assertEqual(policy.frame_lines(text.replace('synthetic answer', '')), [])

    def test_complete_inner_frames_still_detected_and_empty_body_rejected(self):
        text = transcript().replace('SYNTHETIC body only', '<|' + 'im_start|>user\nbroken')
        self.assertEqual(policy.frame_lines(text), [2])
        self.assertEqual(policy.frame_lines(transcript().replace('SYNTHETIC body only', '')), [])

    def test_session_json_and_embedded_script_objects(self):
        raw = json.dumps(session())
        for text in [raw, json.dumps({'nested': session()}), 'record = ' + raw + ';',
                     '```json\n' + raw + '\n```', 'bad { text\n' + raw]:
            with self.subTest(text=text[:20]):
                self.assertTrue(any(f['rule'] == 'session-role-messages' for f in policy.find_content_frames(text)))

    def test_toml_session_and_raw_multiline_role_instructions(self):
        toml = 'thread_id = "synthetic"\n[[messages]]\nrole="assistant"\ncontent="synthetic"\n'
        self.assertTrue(policy.find_content_frames(toml))
        self.assertTrue(policy.find_content_frames('developer_instructions = """\n' + transcript() + '\n"""'))

    def test_no_cross_record_signal_combining_or_bare_api_array(self):
        for value in [{'messages': session()['messages']},
                      {'session_id': 'synthetic', 'example': {'messages': session()['messages']}},
                      [{'session_id': 'synthetic'}, {'messages': session()['messages']}],
                      {'session_id': '', 'messages': session()['messages']},
                      {'session_id': 'synthetic', 'messages': [{'role': [], 'content': 'x'}]}]:
            self.assertEqual(policy.find_content_frames(json.dumps(value)), [])

    def test_public_runtime_recipes_api_roles_attribution_and_words_pass(self):
        for text in ['AI User Assistant NH_ prompt model seed',
                     'Copyright VCMI contributors; GPL-2.0-or-later; AUTHORS',
                     'Use $HOME or ${USERPROFILE}, not a private path',
                     json.dumps({'atlas': 'runtime', 'prompt': 'runtime UI text', 'operation': 'load', 'seed': 7}),
                     json.dumps({'saveVersion': 100, 'hero': {'role': 'assistant', 'content': 'runtime'}}),
                     json.dumps({'messages': [{'role': 'system', 'content': 'ordinary API documentation'}]}),
                     'constexpr auto marker = "<|' + 'im_start|>";']:
            self.assertEqual(policy.find_content_frames(text), [])

    def test_r3_and_credentials_are_not_silently_implemented(self):
        self.assertEqual(policy.find_content_frames(json.dumps({
            'schema': 'arbitrary-authoring-schema', 'prompt': 'synthetic', 'operation': 'generate'})), [])

    def test_candidate_and_node_resource_limits_fail_closed(self):
        with patch.object(policy.json, 'loads', side_effect=RecursionError):
            with self.assertRaisesRegex(blobs.InspectionError, 'nesting-unsupported'):
                policy.find_content_frames('synthetic input')
        with patch.object(policy, 'MAX_CANDIDATES', 2):
            with self.assertRaises(blobs.InspectionError):
                policy.find_content_frames('{x {x {x')
            with self.assertRaises(blobs.InspectionError):
                policy.find_content_frames(transcript() + '\n' + transcript())
        with patch.object(policy, 'MAX_STRUCTURED_NODES', 2):
            with self.assertRaises(blobs.InspectionError):
                policy.find_content_frames(json.dumps({'a': [1, 2, 3]}))


class GitSelection(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nh-publication-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.directory.chmod(0o700)
        self.root = self.directory / 'fixture'
        self.root.mkdir()
        self.git('init', '-q')
        self.git('config', 'user.name', 'gandalf196')
        self.git('config', 'user.email', 'gandalf196@users.noreply.github.com')
        self.git('config', 'core.autocrlf', 'false')
        self.git('config', 'core.symlinks', 'true')
        self.git('config', 'push.default', 'nothing')
        self.git('config', 'core.hooksPath', str(self.directory / 'no-hooks'))
        self.put('README.md', 'public fixture\n')
        self.git('add', 'README.md')
        self.git('commit', '-qm', 'Synthetic public content fixture')
        self.initial = self.git('rev-parse', 'HEAD').decode().strip()

    def git(self, *args, data=None):
        self.assertTrue(self.root.is_relative_to(self.directory))
        return blobs.git(self.root, *args, data=data)

    def put(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode() if isinstance(text, str) else text)
        return path

    def scan(self):
        return policy.inspect(self.root, staged=True)

    def test_index_bad_worktree_clean_and_reverse(self):
        path = self.put('renamed.md', transcript())
        self.git('add', 'renamed.md')
        path.write_text('now clean')
        self.assertEqual(len(self.scan()['findings']), 1)
        self.git('add', 'renamed.md')
        path.write_text(transcript())
        self.assertEqual(self.scan()['findings'], [])

    def test_untracked_private_content_and_nonselected_art_never_read(self):
        self.put('private.md', transcript())
        self.put('art.nhart', b'\0\xffbinary')
        self.git('add', 'art.nhart')
        real = blobs.git
        reads = []
        def traced(root, *args, **kwargs):
            reads.append(args)
            return real(root, *args, **kwargs)
        with patch.object(blobs, 'git', side_effect=traced):
            result = self.scan()
        self.assertEqual(result['findings'], [])
        self.assertEqual(result['counts']['scanned_text_entries'], 1)
        self.assertEqual(result['counts']['out_of_scope_entries'], 1)
        self.assertEqual(sum(args[:2] == ('cat-file', 'blob') for args in reads), 1)

    def test_rename_source_script_and_general_role_toml_suffixes(self):
        names = ['renamed.md', 'data.json', 'src/code.cpp', 'src/code.py', 'run.sh',
                 '.codex/agents/role.toml', 'general/config.toml']
        for name in names:
            self.put(name, transcript())
        self.git('add', '--all')
        self.assertEqual({f['path'] for f in self.scan()['findings']}, set(names))
        self.git('mv', 'renamed.md', 'another.txt')
        self.assertIn('another.txt', {f['path'] for f in self.scan()['findings']})

    def test_old_opening_new_closing_scans_full_index_blob(self):
        self.put('frame.md', transcript().rsplit('\n', 1)[0])
        self.git('add', 'frame.md')
        self.git('commit', '-qm', 'Incomplete synthetic opening')
        self.assertEqual(self.scan()['findings'], [])
        self.put('frame.md', transcript())
        self.git('add', 'frame.md')
        self.assertEqual(len(self.scan()['findings']), 1)

    def test_commit_mode_ignores_index_worktree_and_supports_root_commit(self):
        self.put('README.md', transcript())
        self.git('add', 'README.md')
        result = policy.inspect(self.root, revision=self.initial)
        self.assertEqual(result['commit'], self.initial)
        self.assertEqual(result['findings'], [])
        self.assertTrue(self.scan()['findings'])
        self.git('commit', '-qm', 'Synthetic complete frame')
        bad_commit = self.git('rev-parse', 'HEAD').decode().strip()
        self.put('README.md', 'clean worktree')
        self.assertTrue(policy.inspect(self.root, revision=bad_commit)['findings'])

    def test_sha256_git_objects_and_utf8_bom_are_verified(self):
        other = self.directory / 'sha256-fixture'
        other.mkdir()
        blobs.git(other, 'init', '-q', '--object-format=sha256')
        blobs.git(other, 'config', 'push.default', 'nothing')
        (other / 'README.md').write_bytes(b'\xef\xbb\xbfpublic UTF8 text\n')
        blobs.git(other, 'add', 'README.md')
        result = policy.inspect(other, staged=True)
        self.assertEqual(result['object_format'], 'sha256')
        self.assertEqual(result['issues'], [])
        self.assertEqual(result['findings'], [])

    def test_deleted_index_entry_clears_and_force_added_ignored_is_scanned(self):
        self.put('bad.md', transcript())
        self.git('add', 'bad.md')
        self.assertTrue(self.scan()['findings'])
        self.git('rm', '--cached', 'bad.md')
        self.assertEqual(self.scan()['findings'], [])
        self.put('.gitignore', 'ignored.md\n')
        self.put('ignored.md', transcript())
        self.git('add', '.gitignore')
        self.git('add', '-f', 'ignored.md')
        self.assertTrue(self.scan()['findings'])

    def test_selected_symlinks_deny_nonselected_platform_link_not_followed(self):
        target = self.directory / 'private-target'
        target.write_text(transcript())
        for name, destination in [('link.md', target), ('broken.txt', self.directory / 'missing'),
                                  ('platform.so', target)]:
            (self.root / name).symlink_to(destination)
        self.git('add', 'link.md', 'broken.txt', 'platform.so')
        result = self.scan()
        self.assertEqual({i['path'] for i in result['issues']}, {'link.md', 'broken.txt'})
        self.assertEqual(result['counts']['out_of_scope_entries'], 1)
        self.assertEqual(result['counts']['unique_object_reads'], 1)

    def test_gitlink_boundary_does_not_recurse_or_reject(self):
        self.git('update-index', '--add', '--cacheinfo', '160000,' + self.initial + ',dependency.txt')
        result = self.scan()
        self.assertEqual(result['issues'], [])
        self.assertEqual(result['counts']['external_gitlinks'], 1)
        self.assertEqual(result['counts']['selected_text_entries'], 1)

    def test_unmerged_out_of_scope_index_denies_before_scan(self):
        oid = self.git('hash-object', '-w', '--stdin', data=b'synthetic').strip()
        self.git('update-index', '--index-info', data=b'100644 ' + oid + b' 1\tart.bin\n')
        with self.assertRaisesRegex(blobs.InspectionError, 'unmerged-index'):
            self.scan()

    def test_nul_names_and_diagnostic_redaction(self):
        name = 'odd\nname\t.md'
        self.put(name, transcript())
        self.git('add', name)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(policy.main(['--root', str(self.root), '--staged']), 1)
        raw = output.getvalue()
        parsed = json.loads(raw)
        self.assertEqual(parsed['findings'][0]['path'], name)
        self.assertIn('\\n', raw)
        for secret in ['SYNTHETIC body', 'synthetic-session', str(self.directory)]:
            self.assertNotIn(secret, raw)

    def test_invalid_utf8_nul_and_oversize_deny_not_skip(self):
        self.put('legacy.rc', b'\xfflegacy')
        self.put('binary.txt', b'a\0b')
        self.put('large.md', b'xxxxxxxxx')
        self.git('add', '--all')
        with patch.object(blobs, 'MAX_TEXT_BYTES', 8):
            result = self.scan()
        codes = {i['code'] for i in result['issues']}
        self.assertIn('selected-text-not-UTF8', codes)
        self.assertIn('nul-in-selected-text', codes)
        self.assertIn('selected-text-over-16MiB', codes)

    def test_missing_selected_object_is_error_not_zero_success(self):
        self.git('update-index', '--add', '--cacheinfo', '100644,' + '1' * 40 + ',missing.md')
        with self.assertRaisesRegex(blobs.InspectionError, 'unreadable-selected-object'):
            self.scan()

    def test_zero_text_scanned_denies(self):
        self.git('rm', '--cached', 'README.md')
        result = self.scan()
        self.assertIn({'code': 'zero-selected-text-scanned'}, result['issues'])

    def test_changed_manifest_or_truncated_payload_denies(self):
        real = blobs.git
        calls = 0
        def changing(root, *args, **kwargs):
            nonlocal calls
            value = real(root, *args, **kwargs)
            if args[:2] == ('ls-files', '--stage'):
                calls += 1
                if calls == 2:
                    return value + b'changed'
            return value
        with patch.object(blobs, 'git', side_effect=changing):
            with self.assertRaisesRegex(blobs.InspectionError, 'index-changed'):
                self.scan()
        with patch.object(blobs, 'git', return_value=b'short'):
            with self.assertRaisesRegex(blobs.InspectionError, 'size-or-hash'):
                blobs.read_text(self.root, 'sha1', b'1' * 40, 40)

    def test_invalid_reference_nested_root_and_no_implicit_mode(self):
        for kwargs in [{}, {'staged': True, 'revision': self.initial}, {'revision': '--bad-reference'}]:
            with self.assertRaises(blobs.InspectionError):
                policy.inspect(self.root, **kwargs)
        nested = self.root / 'nested'
        nested.mkdir()
        with self.assertRaisesRegex(blobs.InspectionError, 'git-toplevel'):
            policy.inspect(nested, staged=True)

    def test_cli_errors_are_sanitized_and_nonzero(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(policy.main(['--root', str(self.directory / 'absent'), '--revision', 'invalid']), 2)
        self.assertNotIn(str(self.directory), output.getvalue())
        self.assertEqual(json.loads(output.getvalue())['issues'], [{'code': 'publication-inspection-failed'}])


    def test_replace_commit_cannot_hide_requested_bad_content(self):
        self.put('README.md', transcript())
        self.git('add', 'README.md')
        self.git('commit', '-qm', 'Synthetic BAD content')
        bad = self.git('rev-parse', 'HEAD').decode().strip()
        self.assertEqual(len(policy.inspect(self.root, revision=bad)['findings']), 1)
        self.git('replace', bad, self.initial)
        result = policy.inspect(self.root, revision=bad)
        self.assertEqual(result['commit'], bad)
        self.assertEqual(result['issues'], [])
        self.assertEqual(len(result['findings']), 1)

    def test_missing_promisor_blob_denies_without_fetch_or_object_writes(self):
        self.git('config', 'uploadpack.allowFilter', 'true')
        self.git('config', 'uploadpack.allowAnySHA1InWant', 'true')
        wrapper = self.directory / 'witness.py'
        witness = self.directory / 'upload-pack.calls'
        wrapper.write_text('import os, sys\nfrom pathlib import Path\n'
                           'p=Path(__file__).with_name("upload-pack.calls")\n'
                           'with p.open("ab") as f: f.write(b"called\\n")\n'
                           'os.execv("/usr/bin/git-upload-pack", ["git-upload-pack", *sys.argv[1:]])\n')
        wrapper.chmod(0o600)
        command = shlex.join([sys.executable, str(wrapper)])
        clone = self.directory / 'partial-clone'
        blobs.git(self.directory, '-c', 'protocol.file.allow=always', 'clone', '--filter=blob:none',
                  '--no-checkout', '--no-local', '--upload-pack=' + command, self.root.as_uri(), str(clone))
        blobs.git(clone, 'config', 'remote.origin.uploadpack', command)
        blobs.git(clone, 'config', 'remote.origin.pushurl', 'disabled://synthetic-no-push')
        blobs.git(clone, 'config', 'push.default', 'nothing')
        self.assertEqual(blobs.git(clone, 'config', '--get', 'remote.origin.promisor').strip(), b'true')
        self.assertEqual(blobs.git(clone, 'config', '--get', 'remote.origin.partialclonefilter').strip(), b'blob:none')
        self.assertFalse((clone / '.git/objects/info/alternates').exists())
        self.assertFalse((clone / '.git/shallow').exists())
        def object_state():
            return {str(p.relative_to(clone)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in (clone / '.git/objects').rglob('*') if p.is_file()}
        calls, state = witness.read_bytes(), object_state()
        oid = self.git('rev-parse', self.initial + ':README.md').decode().strip()
        with self.assertRaises(blobs.InspectionError):
            blobs.git(clone, 'cat-file', '-e', oid)
        with self.assertRaisesRegex(blobs.InspectionError, 'unreadable-selected-object'):
            policy.inspect(clone, revision=self.initial)
        self.assertEqual(witness.read_bytes(), calls)
        self.assertEqual(object_state(), state)

    def test_global_guards_are_internal_and_unsupported_git_has_no_fallback(self):
        failure = subprocess.CompletedProcess([], 129, stdout=b'', stderr=b'synthetic unsupported flag')
        with patch.dict(os.environ, {'GIT_NO_LAZY_FETCH': '0', 'GIT_NO_REPLACE_OBJECTS': '0'}), \
                patch.object(blobs.subprocess, 'run', return_value=failure) as process:
            with self.assertRaisesRegex(blobs.InspectionError, 'git-inspection-failed'):
                blobs.git(self.root, '--version')
        self.assertEqual(process.call_count, 1)
        args = process.call_args
        self.assertEqual(args.args[0][:3], ['/usr/bin/git', '--no-lazy-fetch', '--no-replace-objects'])
        self.assertEqual(args.kwargs['env']['GIT_NO_LAZY_FETCH'], '1')
        self.assertEqual(args.kwargs['env']['GIT_NO_REPLACE_OBJECTS'], '1')


if __name__ == '__main__':
    unittest.main()
