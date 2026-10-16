# SPDX-License-Identifier: GPL-2.0-or-later
"""Inert witnesses and disposable Git repositories only; never project scanning."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import credential_budget as budget
import credential_content as policy
import credential_payload as payload
import credential_scope as scope
import git_credential_blobs as reader
import git_publication_blobs as finite
import scanner_runtime as runtime

HERE = Path(policy.__file__).resolve().parent
ARCHIVE = Path(os.environ['NH_CREDENTIAL_TEST_ARCHIVE'])
SENTINEL = 'SYNTHETIC-PRIVATE-OUTPUT-SENTINEL'


from test_credential_path_aware import fixture

class GitSelection(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.git('init','-q')
        self.git('config','user.name','synthetic')
        self.git('config','user.email','synthetic@example.invalid')

    def tearDown(self):
        self.temp.cleanup()

    def git(self,*args,**kwargs):
        return finite.git(self.root,*args,**kwargs)

    def add(self,name,data):
        (self.root/name).write_bytes(data)
        self.git('add','--',name)

    def read(self,**kwargs):
        return reader.read_credentials(self.root,deadline=time.monotonic()+30,**kwargs)

    def inspect(self,**kwargs):
        return policy.inspect(self.root,archive=ARCHIVE,**kwargs)

    def test_selection_index_vs_worktree_unknown_utf8_and_bom(self):
        selected_body=b'\xef\xbb\xbf'+('token="'+policy.synthetic_token()+'"\n').encode()
        self.add('.env',selected_body)
        self.add('.env.alias',selected_body)
        self.add('.gitleaks.toml',b'[allowlist]\nregexes=[".*"]\n')
        self.add('.gitleaksignore',b'# hostile original ignore file is inert payload\n')
        (self.root/'.env').write_bytes(b'clean\n')
        (self.root/'untracked.key').write_bytes(b'not scanned\n')
        result = self.inspect(staged=True)
        self.assertEqual(result['status'],'findings')
        self.assertEqual(result['counts']['unique_objects'],3)
        self.assertEqual(result['counts']['text_aliases'],4)
        self.assertEqual(len(result['findings']),2)
        self.assertEqual({row['path_sha256'] for row in result['findings']},
                         {hashlib.sha256(name.encode()).hexdigest() for name in ('.env','.env.alias')})

    def test_commit_immutable_with_dirty_index_worktree(self):
        self.add('secret.tf',b'ordinary\n'); self.git('commit','-qm','synthetic')
        revision = self.git('rev-parse','HEAD').decode().strip()
        self.add('secret.tf',fixture('hashicorp-tf-password'))
        self.assertEqual(self.inspect(revision=revision)['status'],'clean')

    def test_full_reader_to_scanner_all_path_rules(self):
        for rule,suffix in [('freemius-secret-key','.php'),('hashicorp-tf-password','.tf'),
                            ('kubernetes-secret-yaml','.yaml'),('nuget-config-password','.nuget.config')]:
            self.add('fixture'+suffix,fixture(rule))
        result = self.inspect(staged=True)
        self.assertEqual(result['status'],'findings')
        rulehashes = {r['rule_sha256'] for r in result['findings']}
        for rule in scope.PREDICATES.keys()-{'pkcs12-file'}:
            self.assertIn(hashlib.sha256(rule.encode()).hexdigest(),rulehashes)
        self.assertEqual(result['counts']['evaluated_text_aliases'],4)

    def test_raw_object_deadline_and_original_byte_budget(self):
        self.add('file.unknown',b'ordinary\n')
        for limit,value,error in [('UNIQUE_BYTES',1,'selected-unique-byte-budget'),
                                  ('SINGLE_BYTES',1,'selected-text-over-16MiB'),
                                  ('ENTRIES',0,'selected-entry-budget'),
                                  ('METADATA_BYTES',1,'credential-metadata-budget')]:
            with patch.object(budget,limit,value), patch.object(reader,'read_object',side_effect=AssertionError('pre-read budget')):
                self.assertEqual(self.inspect(staged=True)['issues'][0]['code'],error)
        def stop(data,deadline):
            budget.tick(deadline)
            raise runtime.ScannerError('scanner-deadline')
            yield data
        with patch.object(budget,'chunks',side_effect=stop), patch.object(policy,'witness',side_effect=AssertionError('no scanner')):
            self.assertEqual(self.inspect(staged=True)['issues'][0]['code'],'scanner-deadline')

    def test_pkcs12_presence_empty_and_nonempty_no_binary_read(self):
        for name,data in [('empty.P12',b''),('key.pfx',b'\x00\xff')]:
            self.add(name,data)
        with patch.object(reader,'fetch_objects',side_effect=AssertionError('must not read')):
            result = self.inspect(staged=True)
        self.assertEqual(result['status'],'issue')
        self.assertEqual(result['counts']['presence_entries'],2)
        for issue in result['issues']:
            self.assertEqual(issue['code'],'credential-path-presence')
            self.assertNotIn('start',issue)
        self.assertEqual(result['evaluation_state'],'not-started')
        self.assertEqual(result['counts']['evaluated_text_aliases'],0)
        self.assertEqual(result['counts']['evaluated_unique_objects'],0)

    def test_unsupported_relevant_binary_denied(self):
        self.add('README',b'ordinary\n'); self.add('private.jks',b'\x00\xff')
        self.assertEqual(self.inspect(staged=True)['issues'][0]['code'],'unsupported-relevant-input')

    def test_unknown_nonutf8_or_nul_denied_never_clean(self):
        for body in (b'\xff',b'\x00'):
            self.add('unknown.custom',body)
            result = self.inspect(staged=True)
            self.assertEqual(result['status'],'issue')
            self.assertEqual(result['evaluation_state'],'not-started')
            self.assertEqual(result['counts']['evaluated_text_aliases'],0)
            self.assertIsNone(result['counts']['excluded_entries'])  # selection aborted, not a claimed zero

    def test_explicit_exclusions_are_counted_and_machine_described(self):
        self.add('README',b'ordinary\n'); self.add('image.png',b'\x00\xff')
        result = self.inspect(staged=True)
        self.assertEqual(result['status'],'clean')
        self.assertEqual(result['counts']['excluded_entries'],1)
        self.assertIn('png',result['scope']['excluded_suffixes'])
        self.assertEqual(result['evaluation_state'],'complete')
        self.assertEqual(result['counts']['evaluated_text_aliases'],1)
        self.assertEqual(result['counts']['denied_entries'],0)

    def test_all_excluded_never_clean(self):
        self.add('image.png',b'\x00\xff')
        self.assertEqual(self.inspect(staged=True)['issues'][0]['code'],'zero-credential-text-selected')

    def test_symlink_selected_relevant_mode_denied(self):
        (self.root/'secret.env').symlink_to('/outside/never-read')
        self.git('add','secret.env')
        self.assertEqual(self.inspect(staged=True)['status'],'issue')

    def test_final_index_mutation_denies(self):
        self.add('README',b'ordinary\n')
        real = policy.witness
        def mutate(*args):
            real(*args)
            self.add('README',b'changed\n')
        with patch.object(policy,'witness',side_effect=mutate):
            result = self.inspect(staged=True)
            self.assertEqual(result['issues'][0]['code'],'index-changed-during-scanner')
            self.assertEqual(result['evaluation_state'],'complete')
            self.assertEqual(result['counts']['evaluated_text_aliases'],1)
            self.assertEqual(result['findings'],[])

    def test_1000_large_aliases_prepare_unique_once(self):
        size = budget.SINGLE_BYTES
        body = (b'ordinary public line\n'*(size//21+1))[:size]
        self.add('source.txt',body)
        oid = self.git('rev-parse',':source.txt').strip()
        info = b''.join(b'100644 '+oid+b'\talias-'+str(i).encode()+b'.txt\0' for i in range(999))
        self.git('update-index','-z','--index-info',data=info)
        real_read,real_chunks = reader.read_object,budget.chunks
        calls,large_passes = [],[]
        def track(*args):
            calls.append(1); return real_read(*args)
        def chunks(data,deadline):
            if len(data)==size: large_passes.append(1)
            yield from real_chunks(data,deadline)
        start = time.monotonic()
        with patch.object(reader,'read_object',side_effect=track), patch.object(budget,'chunks',side_effect=chunks):
            result = self.inspect(staged=True)
        self.assertEqual(result['status'],'clean')
        self.assertEqual(len(calls),1)
        self.assertEqual(len(large_passes),3) # raw hash, normalization/UTF8, one write; verification streams separately
        self.assertEqual(result['counts']['representations'],1)
        self.assertEqual(result['counts']['text_aliases'],1000)
        print(json.dumps({'large_alias_stress':dict(aliases=1000,unique_bytes=size,representations=1,
                                                 unique_reads=len(calls),full_chunk_passes=len(large_passes),seconds=time.monotonic()-start)}))

    def test_injected_deadline_during_payload_prevents_selected_launch(self):
        self.add('README',b'ordinary\n')
        real,phase = payload.plan,[]
        def expire(*args):
            phase.append(1)
            return real(*args[:-1],0)
        with patch.object(policy,'witness',return_value=None), patch.object(payload,'plan',side_effect=expire), \
                patch.object(payload,'run',side_effect=AssertionError('must not launch')):
            self.assertEqual(self.inspect(staged=True)['issues'][0]['code'],'scanner-deadline')
        self.assertEqual(len(phase),1)

    def test_zero_deadline_before_selection(self):
        with patch.object(policy,'WHOLE_SECONDS',0), patch.object(reader,'git',side_effect=AssertionError('must not launch')):
            self.assertEqual(self.inspect(staged=True)['issues'][0]['code'],'scanner-deadline')


if __name__ == '__main__':
    unittest.main()
