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


def object_of(data):
    oid = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    return reader.VerifiedObject(oid, data, hashlib.sha256(data).hexdigest(), data.count(b'\n') + 1)


def fixture(rule):
    value = hashlib.sha256(('public inert ' + rule).encode()).hexdigest()
    return {
        'freemius-secret-key': ('"secret_key" => "sk_' + value[:29] + '"\n').encode(),
        'hashicorp-tf-password': ('password = "' + value[:16] + '"\n').encode(),
        'kubernetes-secret-yaml': b'kind: Secret\ndata:\n  payload: UHVibGljSW5lcnRXaXRuZXNz\n',
        'nuget-config-password': ('<add key="ClearTextPassword" value="' + value[:16] + '" />\n').encode(),
    }[rule]


class Predicates(unittest.TestCase):
    def test_manifest_matches_exact_pin(self):
        for malformed in (None, [], 'not-a-lock', 1):
            with self.assertRaisesRegex(runtime.ScannerError, 'unknown-path'):
                scope.validate_manifest(malformed)
        manifest = json.loads((HERE / 'path-predicates.lock.json').read_text())
        scope.validate_manifest(manifest)
        for key, value in [('version','unknown'), ('source_sha256','wrong'), ('source_bytes',0)]:
            changed = dict(manifest, **{key:value})
            with self.assertRaisesRegex(runtime.ScannerError, 'unknown-path'):
                scope.validate_manifest(changed)
        manifest['path_predicates']['unknown'] = '.*'
        with self.assertRaisesRegex(runtime.ScannerError, 'unknown-path'):
            scope.validate_manifest(manifest)

    def test_all_path_rules_case_folding_and_exact_applicability(self):
        for path, rule in [('code.PHP','freemius-secret-key'), ('secret.HCL','hashicorp-tf-password'),
                           ('secret.TF','hashicorp-tf-password'), ('data.YML','kubernetes-secret-yaml'),
                           ('data.YAML','kubernetes-secret-yaml'), ('fooNuGet.Config','nuget-config-password')]:
            self.assertEqual(scope.classify(path), ('text',(rule,)))
            self.assertEqual(scope.applicability(scope.representative('a'*40,(rule,))), (rule,))
        self.assertEqual(scope.applicability('nuget.conf\u0130g'), ())
        self.assertEqual(scope.applicability('file.php\n'), ())
        for name in ('a.p12','a.PFX','folder/a.p12'):
            self.assertEqual(scope.classify(name), ('presence',('pkcs12-file',)))
        self.assertEqual(scope.classify('.p12')[0], 'text')

    def test_unknown_overlapping_class_is_not_silently_narrowed(self):
        with self.assertRaisesRegex(runtime.ScannerError, 'unknown-path'):
            scope.representative('a'*40, ('freemius-secret-key','kubernetes-secret-yaml'))

    def test_unknown_plaintext_extensions_and_credential_names_selected(self):
        for path in ('.env','site.env.local','id_rsa','key.pem','key.key','NuGet.Config',
                     'secret.tf','code.php','secret.custom','ordinary'):
            self.assertEqual(scope.classify(path)[0], 'text')
        for path in ('private.der','private.jks','private.keystore','private.kdbx'):
            self.assertEqual(scope.classify(path)[0], 'denied')
        for path in ('image.png','audio.ogg','bundle.zip','NewHorizons.nhart'):
            self.assertEqual(scope.classify(path)[0], 'excluded')

    def test_existing_finite_selector_unchanged(self):
        for path in ('code.php','secret.tf','private.pem','.env','NuGet.Config'):
            self.assertFalse(finite.selected_text(path))
            self.assertEqual(scope.classify(path)[0], 'text')


class PayloadGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.deadline = time.monotonic() + 10
        self.obj = object_of(b'ordinary\n')
        self.alias = dict(oid=self.obj.oid, rules=(), path_sha256='0'*64)

    def tearDown(self):
        self.temp.cleanup()

    def planned(self):
        return payload.plan({self.obj.oid:self.obj}, [self.alias], self.deadline)

    def test_expired_deadline_precedes_metadata_or_data_work(self):
        with self.assertRaisesRegex(runtime.ScannerError,'scanner-deadline'):
            payload.plan({self.obj.oid:self.obj}, [self.alias]*10000, 0)
        with self.assertRaisesRegex(runtime.ScannerError,'scanner-deadline'):
            payload.materialize(self.directory, self.planned(), 0)
        self.assertEqual(list(self.directory.iterdir()), [])

    def test_alias_dedup_before_any_chunk_work(self):
        with patch.object(budget, 'chunks', side_effect=AssertionError('no object traversal in plan')):
            result = payload.plan({self.obj.oid:self.obj}, [self.alias]*10000, self.deadline)
        self.assertEqual(len(result), 1)

    def test_deadline_during_alias_metadata_before_materialization(self):
        real, calls = budget.tick, []
        def stop(deadline):
            calls.append(1)
            if len(calls) == 200:
                raise runtime.ScannerError('scanner-deadline')
            real(deadline)
        with patch.object(budget,'tick',side_effect=stop), self.assertRaisesRegex(runtime.ScannerError,'deadline'):
            payload.plan({self.obj.oid:self.obj}, [self.alias]*10000, self.deadline)
        self.assertEqual(list(self.directory.iterdir()), [])

    def test_representation_budget_charges_same_oid_each_class(self):
        aliases = [dict(self.alias,rules=rules) for rules in scope.SUFFIX]
        with patch.object(budget,'PHYSICAL_BYTES',len(self.obj.data)*4):
            with self.assertRaisesRegex(runtime.ScannerError,'physical-budget'):
                payload.plan({self.obj.oid:self.obj}, aliases, self.deadline)
        self.assertEqual(len(payload.plan({self.obj.oid:self.obj}, aliases, self.deadline)),5)

    def test_alias_budget_is_separate_from_unique_budget(self):
        with patch.object(budget,'ENTRIES',1):
            with self.assertRaisesRegex(runtime.ScannerError,'selected-entry-budget'):
                payload.plan({self.obj.oid:self.obj}, [self.alias]*2, self.deadline)

    def test_hash_membership_mode_and_link_checks(self):
        expected = payload.materialize(self.directory,self.planned(),self.deadline)
        payload.verify(self.directory,expected,self.deadline)
        path = next(self.directory.iterdir())
        extra=self.directory/'.gitleaks.toml'
        extra.write_text('hostile config')
        with self.assertRaisesRegex(runtime.ScannerError,'membership'):
            payload.verify(self.directory,expected,self.deadline)
        extra.unlink()
        path.chmod(0o600)
        with self.assertRaisesRegex(runtime.ScannerError,'file-type'):
            payload.verify(self.directory,expected,self.deadline)
        path.write_bytes(b'changed!\n')
        path.chmod(0o400)
        with self.assertRaisesRegex(runtime.ScannerError,'payload-hash'):
            payload.verify(self.directory,expected,self.deadline)
        path.unlink()
        with self.assertRaisesRegex(runtime.ScannerError,'membership'):
            payload.verify(self.directory,expected,self.deadline)
        path.symlink_to('/irrelevant')
        with self.assertRaisesRegex(runtime.ScannerError,'file-type'):
            payload.verify(self.directory,expected,self.deadline)

    def test_chunk_deadline_during_write(self):
        real = budget.chunks
        def stop(data,deadline):
            yield from real(data,deadline)
            raise runtime.ScannerError('scanner-deadline')
        with patch.object(budget,'chunks',side_effect=stop), self.assertRaisesRegex(runtime.ScannerError,'deadline'):
            payload.materialize(self.directory,self.planned(),self.deadline)

    def test_parse_missing_bytes_bad_fields_and_no_raw_output(self):
        expected = payload.materialize(self.directory,self.planned(),self.deadline)
        for report, diagnostic in [(b'[]',b'scanned ~8 bytes\n'),(SENTINEL.encode(),b'scanned ~9 bytes\n'),
                                   (b'[]',b'scanned ~9 bytes\npartial scan\n')]:
            with self.assertRaises(runtime.ScannerError) as error:
                payload.parse_result(0,report,diagnostic,expected,self.deadline)
            self.assertNotIn(SENTINEL,str(error.exception))

    def test_report_shapes_duplicate_fields_paths_and_line_ranges(self):
        expected = payload.materialize(self.directory,self.planned(),self.deadline)
        path = next(iter(expected))
        base = dict(file=path,rule='github-pat',start=1,end=1)
        rows = [dict(base,start=True),dict(base,end=100),dict(base,file='../escape'),
                dict(base,secret=SENTINEL),dict(base,start=0)]
        for row in rows:
            with self.assertRaises(runtime.ScannerError):
                payload.parse_result(42,json.dumps([row]).encode(),b'scanned ~9 bytes\n',expected,self.deadline)
        with self.assertRaises(runtime.ScannerError):
            payload.parse_result(0,b'[{"rule":"a","rule":"b"}]',b'scanned ~9 bytes\n',expected,self.deadline)

    def test_report_rejects_unexpected_path_rule_for_class(self):
        expected = payload.materialize(self.directory,self.planned(),self.deadline)
        report = [dict(file=next(iter(expected)),rule='kubernetes-secret-yaml',start=1,end=1)]
        with self.assertRaisesRegex(runtime.ScannerError,'scanner-rule-applicability'):
            payload.parse_result(42,json.dumps(report).encode(),b'scanned ~9 bytes\n',expected,self.deadline)

    def test_deadlines_in_verification_report_and_alias_mapping(self):
        expected = payload.materialize(self.directory,self.planned(),self.deadline)
        for callback in (lambda:payload.verify(self.directory,expected,0),
                         lambda:payload.parse_result(0,b'[]',b'scanned ~9 bytes\n',expected,0),
                         lambda:policy.map_findings([self.alias],{},0)):
            with self.assertRaisesRegex(runtime.ScannerError,'scanner-deadline'):
                callback()

    def test_hardlink_and_directory_permissions_rejected(self):
        expected = payload.materialize(self.directory,self.planned(),self.deadline)
        target = next(self.directory.iterdir())
        external = self.directory.parent/(self.directory.name+'-alias')
        try:
            os.link(target,external)
            with self.assertRaisesRegex(runtime.ScannerError,'file-type'):
                payload.verify(self.directory,expected,self.deadline)
        finally:
            external.unlink(missing_ok=True)
        self.directory.chmod(0o755)
        try:
            with self.assertRaisesRegex(runtime.ScannerError,'file-type'):
                payload.verify(self.directory,expected,self.deadline)
        finally:
            self.directory.chmod(0o700)
        external = self.directory.parent/(self.directory.name+'-directory-alias')
        try:
            external.symlink_to(self.directory,target_is_directory=True)
            with self.assertRaisesRegex(runtime.ScannerError,'file-type'):
                payload.verify_directory(external,self.deadline)
        finally:
            external.unlink(missing_ok=True)

    def test_unchanged_transport_closed_pipes_and_output_caps(self):
        code = 'import os,time;os.close(0);os.close(1);os.close(2);time.sleep(5)'
        with self.assertRaisesRegex(runtime.ScannerError,'scanner-deadline'):
            runtime.run([sys.executable,'-c',code],b'',self.directory,time.monotonic()+0.15)
        for stream in ('stdout','stderr'):
            code = 'import sys;sys.'+stream+'.write("x"*10000)'
            with self.assertRaisesRegex(runtime.ScannerError,'scanner-output-limit'):
                runtime.run([sys.executable,'-c',code],b'',self.directory,self.deadline,
                            stdout_limit=100,stderr_limit=100)

    def test_mapped_finding_budget_and_error_redaction(self):
        findings = {(self.obj.oid,()):[dict(rule_sha256='0'*64,start=1,end=1)]}
        with patch.object(budget,'FINDINGS',1), self.assertRaisesRegex(runtime.ScannerError,'total-finding-limit'):
            policy.map_findings([self.alias]*2,findings,self.deadline)
        with patch.object(reader,'read_credentials',side_effect=runtime.ScannerError(SENTINEL)):
            result = policy.inspect('/irrelevant',staged=True,archive=ARCHIVE)
        self.assertNotIn(SENTINEL,json.dumps(result))
        captured = reader.VerifiedObject(self.obj.oid, self.obj.data, self.obj.sha256,
                                         self.obj.lines, len(self.obj.data))
        selected = dict(counts={}, selection_sha256='0'*64, issues=[],
                        objects={captured.oid:captured}, aliases=[self.alias])
        owned, real = [], policy.prepare
        def track(archive, directory):
            owned.append(directory)
            return real(archive, directory)
        with patch.object(reader,'read_credentials',return_value=selected), \
                patch.object(policy,'witness'), patch.object(policy,'prepare',side_effect=track), \
                patch.object(payload,'scan',return_value=(findings,{})), patch.object(budget,'FINDINGS',0):
            result = policy.inspect('/irrelevant',staged=True,archive=ARCHIVE)
        self.assertEqual(result['issues'],[{'code':'scanner-total-finding-limit'}])
        self.assertEqual(result['findings'],[])
        self.assertTrue(owned)
        self.assertTrue(all(not path.exists() for path in owned))


class RealBatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.owned = Path(cls.temp.name)
        cls.binary, cls.template = runtime.prepare(ARCHIVE,cls.owned)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def scan(self, named):
        objects, aliases = {}, []
        for name,data in named:
            obj = object_of(data)
            objects[obj.oid] = obj
            aliases.append(dict(oid=obj.oid,rules=scope.classify(name)[1],
                                path_sha256=hashlib.sha256(name.encode()).hexdigest()))
        result, counts = payload.scan(self.binary,self.template,objects,aliases,time.monotonic()+30)
        return policy.map_findings(aliases,result,time.monotonic()+5),counts

    def test_all_four_path_content_rules_positive_and_generic_negative(self):
        named = []
        for rule,suffix in [('freemius-secret-key','.PHP'),('hashicorp-tf-password','.HCL'),
                            ('kubernetes-secret-yaml','.YML'),('nuget-config-password','.NuGet.Config')]:
            named += [('file'+suffix,fixture(rule)),('plain-'+rule+'.txt',fixture(rule))]
        result,counts = self.scan(named)
        for rule,suffix in [('freemius-secret-key','.PHP'),('hashicorp-tf-password','.HCL'),
                            ('kubernetes-secret-yaml','.YML'),('nuget-config-password','.NuGet.Config')]:
            rulehash = hashlib.sha256(rule.encode()).hexdigest()
            selected = [r['path_sha256'] for r in result if r['rule_sha256']==rulehash]
            self.assertEqual(selected,[hashlib.sha256(('file'+suffix).encode()).hexdigest()])
        self.assertEqual(counts['representations'],8)

    def test_all_class_clean_negatives(self):
        result,counts = self.scan([(name,b'ordinary public documentation\n') for name in
                                   ('file.txt','file.php','file.tf','file.yaml','file.nuget.config')])
        self.assertEqual(result,[])
        self.assertEqual(counts['representations'],5)

    def test_same_oid_classes_map_only_eligible_original_aliases(self):
        data = b''.join(fixture(rule) for rule in scope.PREDICATES if rule != 'pkcs12-file')
        result,counts = self.scan([(name,data) for name in
                                   ('same.txt','same.php','same.tf','same.yaml','same.nuget.config')])
        self.assertEqual(counts['representations'],5)
        self.assertEqual(counts['physical_bytes'],len(data)*5)
        for rule in scope.PREDICATES.keys()-{'pkcs12-file'}:
            rows = [r for r in result if r['rule_sha256']==hashlib.sha256(rule.encode()).hexdigest()]
            self.assertEqual(len(rows),1)

    def test_hostile_original_names_never_materialized(self):
        data = ('token="'+policy.synthetic_token()+'" # gitleaks:allow\n').encode()
        names = ['../.gitleaks.toml/secret.php','.gitleaksignore\n'+SENTINEL+'.yaml','x\tNuGet.Config']
        real,argvs = payload.run,[]
        def track(argv,*args,**kwargs):
            argvs.append(argv)
            self.assertEqual(list(Path(args[1]).iterdir()),[])
            for p in Path(argv[2]).iterdir():
                self.assertRegex(p.name,r'^[a-f0-9]{40}(?:\.txt|\.php|\.tf|\.yaml|\.nuget\.config)$')
            return real(argv,*args,**kwargs)
        with patch.object(payload,'run',side_effect=track), patch.dict(os.environ,{'GITLEAKS_CONFIG_TOML':'[allowlist]\nregexes=[".*"]'}):
            result,_ = self.scan([(name,data) for name in names])
        self.assertEqual(len(argvs),1)
        self.assertEqual(len(result),3)
        self.assertNotIn(SENTINEL,json.dumps(result))

    def test_mime_deficit_still_denies(self):
        with self.assertRaisesRegex(runtime.ScannerError,'byte-completion'):
            self.scan([('clean.yaml',b'ordinary\n'),('mime.php',b'%PDF-1.7\nsynthetic\n')])

    def test_postscan_mutation_rejected_and_cleaned(self):
        real,dirs = payload.run,[]
        def mutate(argv,*args,**kwargs):
            result = real(argv,*args,**kwargs)
            directory = Path(argv[2]); dirs.append(directory)
            path = next(directory.iterdir())
            if mutation == 'missing':
                path.unlink()
            else:
                path.chmod(0o600); path.write_bytes(b'changed'); path.chmod(0o400)
            return result
        for mutation, error in [('changed','payload-hash'), ('missing','payload-membership')]:
            with patch.object(payload,'run',side_effect=mutate), self.assertRaisesRegex(runtime.ScannerError,error):
                self.scan([('file.php',b'ordinary\n')])
        self.assertTrue(all(not directory.exists() for directory in dirs))

    def test_version_and_two_file_witness(self):
        policy.witness(self.binary,self.template,self.owned,time.monotonic()+10)



if __name__ == '__main__':
    unittest.main()
