# SPDX-License-Identifier: GPL-2.0-or-later
"""Retained refusal, transport, privacy and complete-file regression controls."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

import credential_budget as budget
import credential_content as policy
import credential_payload as payload
import git_credential_blobs as reader
import git_publication_blobs as finite
import scanner_runtime as runtime
from test_credential_path_aware import ARCHIVE, HERE, SENTINEL, object_of


class Transport(unittest.TestCase):
    def test_lock_notice_and_fixed_command_surface(self):
        lock = json.loads((HERE/'gitleaks.lock.json').read_text())
        self.assertEqual(lock['sha256'],runtime.ARCHIVE_SHA)
        self.assertEqual(lock['bytes'],runtime.ARCHIVE_SIZE)
        self.assertEqual(hashlib.sha256((HERE/'LICENSE.gitleaks').read_bytes()).hexdigest(),runtime.LICENSE_SHA)
        argv = runtime.command('/binary','/template','/cwd','/payload')
        self.assertEqual(argv[1:3],['dir','/payload'])
        self.assertIn('--ignore-gitleaks-allow',argv)
        self.assertIn('--max-decode-depth=0',argv)
        self.assertIn('--max-archive-depth=0',argv)
        for name in ('Secret','Match','Fragment','Description','Fingerprint'):
            self.assertNotIn('.'+name,runtime.TEMPLATE)

    def test_archive_identity_links_platform_and_missing_executable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            wrong,alias = root/'wrong',root/'alias'
            wrong.write_bytes(b'wrong')
            alias.symlink_to(ARCHIVE)
            with patch.object(runtime.tarfile,'open',side_effect=AssertionError('no unauthenticated parse')):
                for source in (wrong,alias,root/'absent'):
                    with self.assertRaises(runtime.ScannerError): runtime.prepare(source,root)
            with patch.object(runtime.platform,'machine',return_value='unsupported'):
                with self.assertRaisesRegex(runtime.ScannerError,'unsupported-scanner-platform'):
                    runtime.prepare(ARCHIVE,root)
            with self.assertRaisesRegex(runtime.ScannerError,'scanner-process-unavailable'):
                runtime.run([str(root/'absent')],b'',root,time.monotonic()+1)

    def test_authenticated_archive_duplicate_or_link_layout_denied(self):
        members = []
        for name,size in [('LICENSE',1069),('README.md',29998),('gitleaks',21958840)]:
            member = tarfile.TarInfo(name); member.size=size; members.append(member)
        with tempfile.TemporaryDirectory() as temporary:
            for invalid in (members+[members[-1]],members[:-1]+[tarfile.TarInfo('unknown')]):
                archive = Mock(getmembers=Mock(return_value=invalid))
                with patch.object(runtime.tarfile,'open') as opening:
                    opening.return_value.__enter__.return_value=archive
                    with self.assertRaisesRegex(runtime.ScannerError,'archive-members'):
                        runtime.prepare(ARCHIVE,Path(temporary))
            members[-1].type=tarfile.SYMTYPE
            with patch.object(runtime.tarfile,'open') as opening:
                opening.return_value.__enter__.return_value=Mock(getmembers=Mock(return_value=members))
                with self.assertRaisesRegex(runtime.ScannerError,'archive-members'):
                    runtime.prepare(ARCHIVE,Path(temporary))

    def test_minimal_environment_soft_memory_and_rss_abort(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ,{'GITLEAKS_CONFIG':SENTINEL,'GIT_CONFIG_COUNT':'1'}):
            _,out,_=runtime.run([sys.executable,'-c','import os,json;print(json.dumps(dict(os.environ)))'],
                                b'',temporary,time.monotonic()+2)
        env=json.loads(out)
        self.assertEqual(env['GOMAXPROCS'],'1')
        self.assertEqual(env['GOMEMLIMIT'],'256MiB')
        self.assertNotIn(SENTINEL,out.decode())
        self.assertFalse(any(k.startswith(('GIT_','GITLEAKS_')) for k in env))
        with patch('builtins.open',return_value=io.BytesIO(b'VmRSS: 600000 kB\n')):
            with self.assertRaisesRegex(runtime.ScannerError,'scanner-rss-abort'):
                runtime.check_rss(Mock(pid=123,poll=Mock(return_value=None)))

    def test_input_backpressure_bounded_and_reaped(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(runtime.ScannerError,'scanner-deadline'):
                runtime.run([sys.executable,'-c','import time;time.sleep(5)'],b'x'*200000,
                            temporary,time.monotonic()+0.15)

    def test_version_and_nonfunctional_positive_and_negative_refused(self):
        with patch.object(policy,'run',return_value=(0,b'8.30.0\n',b'')):
            with self.assertRaisesRegex(runtime.ScannerError,'scanner-version'):
                policy.witness('/binary','/template','/owned',time.monotonic()+1)
        with patch.object(policy,'run',return_value=(0,b'8.30.1\n',b'')),patch.object(payload,'scan',return_value=({},{})):
            with self.assertRaisesRegex(runtime.ScannerError,'positive-witness'):
                policy.witness('/binary','/template','/owned',time.monotonic()+1)
        def both(objects_arg,template,objects,aliases,deadline,**kwargs):
            return { (a['oid'],()):[dict(rule_sha256=hashlib.sha256(b'github-pat').hexdigest())]
                     for a in aliases },{}
        with patch.object(policy,'run',return_value=(0,b'8.30.1\n',b'')),patch.object(payload,'scan',side_effect=both):
            with self.assertRaisesRegex(runtime.ScannerError,'negative-witness'):
                policy.witness('/binary','/template','/owned',time.monotonic()+1)


class Reports(unittest.TestCase):
    def setUp(self):
        self.expected={'/opaque/file.txt':dict(key=('a'*40,()),bytes=4,lines=1)}
        self.deadline=time.monotonic()+5

    def parse(self,code,report,diagnostic=b'scanned ~4 bytes\n'):
        return payload.parse_result(code,report,diagnostic,self.expected,self.deadline)

    def test_exit_disagreement_malformed_and_duplicate_summaries(self):
        for code,report in [(42,b'[]'),(1,b'[]'),(0,b'[{"file":"/opaque/file.txt","rule":"x","start":1,"end":1}]')]:
            with self.assertRaises(runtime.ScannerError): self.parse(code,report)
        for diagnostic in (b'',b'scanned ~0 bytes\n',b'scanned ~4 bytes\nscanned ~4 bytes\n'):
            with self.assertRaisesRegex(runtime.ScannerError,'byte-completion'): self.parse(0,b'[]',diagnostic)
        for report in (b'null',b'{}',b'[[[]]]',b'bad'):
            with self.assertRaises(runtime.ScannerError): self.parse(0,report)

    def test_finding_caps_digest_rule_and_diagnostic_sentinel(self):
        row=dict(file='/opaque/file.txt',rule=SENTINEL.lower(),start=1,end=1)
        result=self.parse(42,json.dumps([row]).encode(),b'scanned ~4 bytes\n'+SENTINEL.encode())
        self.assertNotIn(SENTINEL.lower(),json.dumps(list(result.values())))
        with self.assertRaisesRegex(runtime.ScannerError,'report-count'):
            self.parse(42,json.dumps([row]*4097).encode())


class CompleteFiles(unittest.TestCase):
    def test_real_aws_privatekey_empty_boundaries_and_129_object_batch(self):
        suffix=''.join('ABCDEFGHIJKLMNOPQRSTUVWXYZ234567'[b%32] for b in hashlib.sha256(b'synthetic noncredential aws').digest()[:16])
        bodies=[b'',('aws_access_key_id="'+'AK'+'IA'+suffix+'"\n').encode(),
                ('-----BEGIN '+'RSA PRIVATE KEY-----\n'+'QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo='*3+'\n-----END RSA PRIVATE KEY-----\n').encode(),
                b'x\n'*49990+('token="'+policy.synthetic_token()+'"\n').encode()]
        bodies += [f'ordinary public object {i}\n'.encode() for i in range(125)]
        bodies[4]=b'Copyright VCMI contributors GPL-2.0-or-later MIT Zachary Rice 2019\n'+json.dumps(
            {'messages':[{'role':'assistant','content':'ordinary API example'}]}).encode()+b'\nsha256='+hashlib.sha256(b'public source').hexdigest().encode()
        objects={o.oid:o for o in map(object_of,bodies)}
        aliases=[dict(oid=o.oid,rules=()) for o in objects.values()]
        with tempfile.TemporaryDirectory() as temporary:
            binary,template=runtime.prepare(ARCHIVE,Path(temporary))
            real,calls=payload.run,[]
            def track(*args,**kwargs): calls.append(1); return real(*args,**kwargs)
            start=time.monotonic()
            with patch.object(payload,'run',side_effect=track):
                findings,counts=payload.scan(binary,template,objects,aliases,start+30)
        self.assertEqual(len(calls),1)
        self.assertEqual(counts['representations'],129)
        rulehashes={row['rule_sha256'] for rows in findings.values() for row in rows}
        for rule in ('aws-access-token','private-key','github-pat'):
            self.assertIn(hashlib.sha256(rule.encode()).hexdigest(),rulehashes)
        self.assertEqual(findings[(object_of(bodies[3]).oid,())][0]['start'],49991)
        self.assertNotIn((object_of(bodies[4]).oid,()),findings)
        print(json.dumps({'batch_stress':dict(objects=129,selected_processes=1,physical_bytes=counts['physical_bytes'],seconds=time.monotonic()-start)}))

    def test_all_empty_vacuous_and_equal_sized_mime_deficit(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary,template=runtime.prepare(ARCHIVE,Path(temporary))
            empty=object_of(b'')
            findings,_=payload.scan(binary,template,{empty.oid:empty},[dict(oid=empty.oid,rules=())],time.monotonic()+10)
            self.assertEqual(findings,{})
            objects={o.oid:o for o in map(object_of,[b'%PDF-1.7\n',b'a'*9,b'b'*9])}
            with self.assertRaisesRegex(runtime.ScannerError,'byte-completion'):
                payload.scan(binary,template,objects,[dict(oid=o.oid,rules=()) for o in objects.values()],time.monotonic()+10)


class NewReaderControls(unittest.TestCase):
    def test_sha256_bom_and_missing_selected_object(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            finite.git(root,'init','-q','--object-format=sha256')
            (root/'unknown.custom').write_bytes(b'\xef\xbb\xbfordinary\n')
            finite.git(root,'add','unknown.custom')
            selected=reader.read_credentials(root,staged=True,deadline=time.monotonic()+5)
            self.assertEqual(next(iter(selected['objects'].values())).data,b'ordinary\n')
            oid=next(iter(selected['objects']))
            location=root/'.git/objects'/oid[:2]/oid[2:]
            location.unlink()
            with self.assertRaises(runtime.ScannerError):
                reader.read_credentials(root,staged=True,deadline=time.monotonic()+5)

    def test_unmerged_entries_and_invalid_root_or_mode_denied(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); finite.git(root,'init','-q')
            oid=finite.git(root,'hash-object','-w','--stdin',data=b'ordinary\n').strip()
            finite.git(root,'update-index','--index-info',data=b'100644 '+oid+b' 1\timage.png\n')
            result=policy.inspect(root,staged=True,archive=ARCHIVE)
            self.assertEqual(result['issues'][0]['code'],'unmerged-index')
            for args in ({},{'staged':True,'revision':'HEAD'}):
                self.assertEqual(policy.inspect(root,archive=ARCHIVE,**args)['status'],'issue')

    def test_large_alias_deadline_in_normal_orchestration_after_unique_preparation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); finite.git(root,'init','-q')
            body=(b'ordinary public line\n'*(budget.SINGLE_BYTES//21+1))[:budget.SINGLE_BYTES]
            oid=finite.git(root,'hash-object','-w','--stdin',data=body).strip()
            info=b''.join(b'100644 '+oid+b'\ta'+str(i).encode()+b'.txt\0' for i in range(1000))
            finite.git(root,'update-index','-z','--index-info',data=info)
            real_plan,real_read=payload.plan,reader.read_object
            reads,calls,owned=[],[],[]
            real_prepare=policy.prepare
            def prepare(archive,directory): owned.append(directory); return real_prepare(archive,directory)
            def read(*args): reads.append(1); return real_read(*args)
            def expire(objects,aliases,deadline):
                self.assertEqual(len(objects),1); self.assertEqual(len(aliases),1000)
                real_tick=budget.tick
                def tick(same_deadline):
                    self.assertEqual(same_deadline,deadline); calls.append(1)
                    if len(calls)==100: raise runtime.ScannerError('scanner-deadline')
                    real_tick(same_deadline)
                with patch.object(budget,'tick',side_effect=tick): return real_plan(objects,aliases,deadline)
            with patch.object(policy,'witness',return_value=None),patch.object(reader,'read_object',side_effect=read), \
                    patch.object(payload,'plan',side_effect=expire),patch.object(policy,'prepare',side_effect=prepare), \
                    patch.object(payload,'run',side_effect=AssertionError('no selected launch')):
                result=policy.inspect(root,staged=True,archive=ARCHIVE)
            self.assertEqual(result['issues'][0]['code'],'scanner-deadline')
            self.assertEqual(len(reads),1); self.assertEqual(len(calls),100)
            self.assertTrue(owned); self.assertFalse(owned[0].exists())


if __name__=='__main__':
    unittest.main()
