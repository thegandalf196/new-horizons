"""Integrity checks for the local Linux playable snapshot helper."""
from contextlib import redirect_stdout
import io
import hashlib
import json
import os
import signal
from pathlib import Path
import sys
import subprocess
import tempfile
import time
import unittest
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ci"))
from linux_playable_snapshot import (freeze, make_removable, package_files, promote,
                                     resolve, verify_snapshot, write_metadata, snapshot_client)
import linux_playable_snapshot as snapshot_tools
from client_features import FILENAME as CLIENT_FEATURES, feature_payload


class LinuxPlayableSnapshotTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = self.root / "build" / "bin"
        self.bin.mkdir(parents=True)
        self.client = self.bin / "new-horizons"
        self.client.write_text("#!/bin/sh\nexit 0\n")
        self.client.chmod(0o755)
        (self.bin / "libvcmi.so").write_bytes(b"matching library")
        self.source = self.root / "source"
        for relative in ("config", "scripts/damage", "Mods/vcmi", "Mods/new-horizons/Images"):
            (self.source / relative).mkdir(parents=True, exist_ok=True)
        for relative, content in (
            ("config/filesystem.json", "{}"),
            ("config/newHorizonsCombat.json", "{}"),
            ("config/newHorizonsMagic.json", "{}"),
            ("scripts/damage/damageCalculator.lua", "return {}"),
            ("Mods/vcmi/mod.json", "{}"),
            ("Mods/new-horizons/mod.json", "{}"),
            ("Mods/new-horizons/Images/icon.png", "synthetic image"),
        ):
            (self.source / relative).write_text(content)
        self.resources = self.root / "build" / "resources"
        self.resources.mkdir()
        for name in ("config", "scripts", "Mods"):
            (self.resources / name).symlink_to(self.source / name, target_is_directory=True)
        self.store = self.root / "playable-snapshots"

    def tearDown(self):
        if self.store.exists():
            for path in self.store.glob("snapshot-*"):
                if path.is_dir() and not path.is_symlink():
                    make_removable(path)

    def freeze_candidate(self, retain_resources_from=None, runtime_libraries=()):
        output = io.StringIO()
        with redirect_stdout(output):
            freeze(SimpleNamespace(client=self.client, resources=self.resources,
                                   store=self.store, promote=False,
                                   runtime_libraries=runtime_libraries,
                                   retain_resources_from=retain_resources_from))
        return Path(output.getvalue().strip())

    def runtime_library(self, name='libSDL3.so.0', directory=None):
        directory = directory or self.root / 'providers'
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / name
        # Synthetic ELF64 little-endian ET_DYN header, never executed.
        header = bytearray(64)
        header[:7] = b'\x7fELF\x02\x01\x01'
        header[16:18] = (3).to_bytes(2, 'little')
        header[20:24] = (1).to_bytes(4, 'little')
        path.write_bytes(header + b'synthetic provider')
        return path

    def test_optional_build_features_are_copied_bound_and_readonly(self):
        marker = self.bin / CLIENT_FEATURES
        marker.write_bytes(feature_payload(hashlib.sha256(self.client.read_bytes()).hexdigest()))
        candidate = self.freeze_candidate()
        self.assertEqual((candidate / CLIENT_FEATURES).read_bytes(), marker.read_bytes())
        self.assertEqual((candidate / CLIENT_FEATURES).stat().st_mode & 0o222, 0)
        self.assertIn(CLIENT_FEATURES, verify_snapshot(candidate)['files'])
        self.assertFalse((self.store / 'current.json').exists())

    def test_feature_declaration_for_another_client_refuses_freeze(self):
        (self.bin / CLIENT_FEATURES).write_bytes(feature_payload('0' * 64))
        with self.assertRaisesRegex(RuntimeError, 'differs from the selected client'):
            self.freeze_candidate()
        self.assertFalse((self.store / 'current.json').exists())

    def test_semantically_wrong_marker_refuses_even_with_matching_snapshot_checksums(self):
        marker = self.bin / CLIENT_FEATURES
        marker.write_bytes(feature_payload(hashlib.sha256(self.client.read_bytes()).hexdigest()))
        candidate = self.freeze_candidate()
        make_removable(candidate)
        (candidate / CLIENT_FEATURES).write_bytes(feature_payload('0' * 64))
        digest = write_metadata(candidate, package_files(candidate))
        renamed = self.store / ('snapshot-' + digest)
        candidate.rename(renamed)
        with self.assertRaisesRegex(RuntimeError, 'differs from the selected client'):
            verify_snapshot(renamed)

    def test_linked_feature_declaration_refuses_freeze(self):
        (self.bin / CLIENT_FEATURES).symlink_to(self.client)
        with self.assertRaisesRegex(RuntimeError, 'regular non-symlink'):
            self.freeze_candidate()

    def test_markerless_legacy_snapshot_remains_valid(self):
        candidate = self.freeze_candidate()
        self.assertNotIn(CLIENT_FEATURES, verify_snapshot(candidate)['files'])
        self.assertFalse((candidate / CLIENT_FEATURES).exists())

    def test_four_explicit_runtime_providers_are_hashed_readonly_and_unselected(self):
        providers = [self.runtime_library(name) for name in (
            'libSDL3.so.0', 'libSDL3_image.so.0', 'libSDL3_mixer.so.0', 'libSDL3_ttf.so.0')]
        candidate = self.freeze_candidate(runtime_libraries=providers)
        metadata = verify_snapshot(candidate)
        for provider in providers:
            copied = candidate / provider.name
            self.assertEqual(copied.read_bytes(), provider.read_bytes())
            self.assertEqual(metadata['files'][provider.name], hashlib.sha256(provider.read_bytes()).hexdigest())
            self.assertEqual(copied.stat().st_mode & 0o222, 0)
        self.assertFalse((self.store / 'current.json').exists())

    def test_runtime_provider_missing_is_refused(self):
        with self.assertRaisesRegex(RuntimeError, 'regular non-symlink'):
            self.freeze_candidate(runtime_libraries=[self.root / 'libMissing.so'])

    def test_runtime_provider_symlink_is_refused(self):
        provider = self.runtime_library()
        link = provider.with_name('libLink.so.0')
        link.symlink_to(provider)
        with self.assertRaisesRegex(RuntimeError, 'regular non-symlink'):
            self.freeze_candidate(runtime_libraries=[link])

    def test_runtime_provider_unsafe_basename_is_refused(self):
        for name in ('provider.so', 'libBad.so.notnumeric', 'libBad.so.1;bad'):
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, 'safe lib'):
                self.freeze_candidate(runtime_libraries=[self.runtime_library(name)])

    def test_runtime_provider_non_elf_or_executable_header_is_refused(self):
        provider = self.runtime_library()
        for payload in (b'not ELF', b'\x7fELF', provider.read_bytes()[:16] + b'\x02\x00' + provider.read_bytes()[18:]):
            provider.write_bytes(payload)
            with self.subTest(payload=payload), self.assertRaisesRegex(RuntimeError, 'ELF shared-object'):
                self.freeze_candidate(runtime_libraries=[provider])

    def test_runtime_provider_basename_collisions_are_refused(self):
        provider = self.runtime_library()
        duplicate = self.runtime_library(directory=self.root / 'other-providers')
        for providers in ([self.runtime_library('libvcmi.so')], [provider, duplicate], [provider, provider]):
            with self.subTest(providers=providers), self.assertRaisesRegex(RuntimeError, 'basename collision'):
                self.freeze_candidate(runtime_libraries=providers)

    def test_runtime_provider_change_during_copy_is_refused(self):
        provider = self.runtime_library()
        write_payload = snapshot_tools.write_payload

        def copy_then_change(files, target):
            write_payload(files, target)
            provider.write_bytes(provider.read_bytes() + b'changed after copy')

        with mock.patch.object(snapshot_tools, 'write_payload', side_effect=copy_then_change):
            with self.assertRaisesRegex(RuntimeError, 'changed while'):
                self.freeze_candidate(runtime_libraries=[provider])
        self.assertFalse(list(self.store.glob('snapshot-*')))
        self.assertFalse(list(self.store.glob('.snapshot-*')))

    def test_cli_accepts_repeated_runtime_library_options(self):
        providers = [self.runtime_library('libOne.so'), self.runtime_library('libTwo.so.1.2')]
        script = Path(__file__).resolve().parents[1] / 'ci/linux_playable_snapshot.py'
        command = [sys.executable, str(script), 'freeze', '--client', str(self.client),
                   '--resources', str(self.resources), '--store', str(self.store), '--no-promote']
        for provider in providers:
            command.extend(['--runtime-library', str(provider)])
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        metadata = verify_snapshot(Path(result.stdout.strip()))
        self.assertTrue(all(provider.name in metadata['files'] for provider in providers))

    def promote_candidate(self, snapshot):
        output = io.StringIO()
        with redirect_stdout(output):
            promote(SimpleNamespace(snapshot=snapshot, store=self.store))
        return Path(output.getvalue().strip())

    def selected_snapshot(self):
        output = io.StringIO()
        with redirect_stdout(output):
            resolve(SimpleNamespace(store=self.store))
        return Path(output.getvalue().strip())

    def validate_candidate(self, snapshot, code, timeout=5, extra_args=()):
        output = io.StringIO()
        with redirect_stdout(output):
            snapshot_tools.validate(SimpleNamespace(
                snapshot=snapshot, store=self.store, timeout=timeout,
                validation_command=[sys.executable, '-c', code, *extra_args]))
        return Path(output.getvalue().strip())

    def test_validation_success_binds_exact_candidate_and_overwrites_ambient_identity(self):
        candidate = self.freeze_candidate()
        receipt = self.root / 'driver.json'
        code = ("import json,os,pathlib,sys; "
                "pathlib.Path(sys.argv[1]).write_text(json.dumps({k:os.environ[k] for k in "
                "('NH_VALIDATION_SNAPSHOT','NH_VALIDATION_SNAPSHOT_SHA256')}))")
        with mock.patch.dict(os.environ, {'NH_VALIDATION_SNAPSHOT': '/wrong',
                                         'NH_VALIDATION_SNAPSHOT_SHA256': 'wrong'}):
            self.assertEqual(self.validate_candidate(candidate, code, extra_args=(str(receipt),)), candidate)
        self.assertEqual(self.selected_snapshot(), candidate)
        self.assertEqual(json.loads(receipt.read_text()), {
            'NH_VALIDATION_SNAPSHOT': str(candidate),
            'NH_VALIDATION_SNAPSHOT_SHA256': candidate.name.removeprefix('snapshot-')})

    def test_validation_failure_keeps_selected_baseline(self):
        baseline = self.freeze_candidate()
        self.promote_candidate(baseline)
        (self.source / 'config/filesystem.json').write_text('{"candidate":2}')
        candidate = self.freeze_candidate()
        before = (self.store / 'current.json').read_bytes()
        with self.assertRaisesRegex(RuntimeError, 'exit status 7'):
            self.validate_candidate(candidate, 'raise SystemExit(7)')
        self.assertEqual((self.store / 'current.json').read_bytes(), before)
        self.assertEqual(self.selected_snapshot(), baseline)

    def test_validation_spawn_exception_does_not_promote(self):
        candidate = self.freeze_candidate()
        with mock.patch.object(snapshot_tools.subprocess, 'Popen', side_effect=OSError('cannot spawn')):
            with self.assertRaisesRegex(RuntimeError, 'cannot spawn'):
                self.validate_candidate(candidate, 'pass')
        self.assertFalse((self.store / 'current.json').exists())

    def test_validation_rejects_payload_mutation_even_after_exit_zero(self):
        candidate = self.freeze_candidate()
        code = ("import os,pathlib; p=pathlib.Path(os.environ['NH_VALIDATION_SNAPSHOT'])/"
                "'config/filesystem.json'; p.chmod(0o644); p.write_text('changed')")
        with self.assertRaisesRegex(RuntimeError, 'inventory or checksum mismatch'):
            self.validate_candidate(candidate, code)
        self.assertFalse((self.store / 'current.json').exists())

    def test_validation_rejects_metadata_only_mutation(self):
        candidate = self.freeze_candidate()
        code = ("import os,pathlib; p=pathlib.Path(os.environ['NH_VALIDATION_SNAPSHOT'])/"
                "'SNAPSHOT.json'; p.chmod(0o644); p.write_text(p.read_text()+'\\n')")
        with self.assertRaisesRegex(RuntimeError, 'Candidate changed'):
            self.validate_candidate(candidate, code)
        self.assertFalse((self.store / 'current.json').exists())

    def test_validation_cannot_overwrite_concurrently_promoted_candidate(self):
        candidate = self.freeze_candidate()
        (self.source / 'config/filesystem.json').write_text('{"newer":true}')
        newer = self.freeze_candidate()
        script = Path(snapshot_tools.__file__).resolve()
        code = ("import subprocess,sys; subprocess.run([sys.executable,sys.argv[1],"
                "'promote','--store',sys.argv[2],'--snapshot',sys.argv[3]],check=True)")
        with self.assertRaisesRegex(RuntimeError, 'refusing stale promotion'):
            self.validate_candidate(candidate, code, extra_args=(str(script), str(self.store), str(newer)))
        self.assertEqual(self.selected_snapshot(), newer)

    def test_validation_detects_same_bytes_pointer_replacement(self):
        candidate = self.freeze_candidate()
        self.promote_candidate(candidate)
        code = ("import os,pathlib,sys,tempfile; p=pathlib.Path(sys.argv[1])/'current.json'; "
                "fd,n=tempfile.mkstemp(dir=p.parent); "
                "os.write(fd,p.read_bytes()); os.close(fd); os.replace(n,p)")
        with self.assertRaisesRegex(RuntimeError, 'refusing stale promotion'):
            self.validate_candidate(candidate, code, extra_args=(str(self.store),))
        self.assertEqual(self.selected_snapshot(), candidate)

    def test_validation_refuses_malformed_or_symlink_pointer_before_spawn(self):
        candidate = self.freeze_candidate()
        pointer = self.store / 'current.json'
        for linked in (False, True):
            with self.subTest(linked=linked):
                if linked:
                    pointer.unlink()
                    pointer.symlink_to(self.root / 'missing')
                else:
                    pointer.write_text('not json')
                with mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn:
                    with self.assertRaises(RuntimeError):
                        self.validate_candidate(candidate, 'pass')
                    spawn.assert_not_called()

    def test_validation_requires_explicit_command_and_finite_positive_timeout(self):
        candidate = self.freeze_candidate()
        for timeout, command in ((0, [sys.executable]), (-1, [sys.executable]),
                                 (float('nan'), [sys.executable]), (float('inf'), [sys.executable]),
                                 (1, []), (1, ['--'])):
            with self.subTest(timeout=timeout, command=command):
                with mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn:
                    with self.assertRaises(RuntimeError):
                        snapshot_tools.validate(SimpleNamespace(snapshot=candidate, store=self.store,
                                                               timeout=timeout, validation_command=command))
                    spawn.assert_not_called()

    def test_validation_timeout_reaps_driver_and_its_owned_descendant(self):
        candidate = self.freeze_candidate()
        ids = self.root / 'pids.json'
        code = (
            "import json,os,pathlib,signal,subprocess,sys,time\n"
            "child=subprocess.Popen([sys.executable,'-c',"
            "'import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(30)'])\n"
            "def stop(signum,frame):\n"
            " child.kill(); child.wait(); raise SystemExit(0)\n"
            "signal.signal(signal.SIGTERM,stop)\n"
            "pathlib.Path(sys.argv[1]).write_text(json.dumps([os.getpid(),child.pid]))\n"
            "time.sleep(30)\n")
        with self.assertRaisesRegex(RuntimeError, 'did not complete successfully'):
            self.validate_candidate(candidate, code, timeout=.5, extra_args=(str(ids),))
        for pid in json.loads(ids.read_text()):
            with self.assertRaises(ProcessLookupError):
                os.kill(pid, 0)
        self.assertFalse((self.store / 'current.json').exists())

    def test_validation_uses_literal_argv_not_shell(self):
        candidate = self.freeze_candidate()
        argument = '$HOME; touch ' + str(self.root / 'must-not-exist')
        code = "import sys; assert sys.argv[1].startswith('$HOME; touch ')"
        self.validate_candidate(candidate, code, extra_args=(argument,))
        self.assertFalse((self.root / 'must-not-exist').exists())

    def test_validation_left_descendant_branch_cleans_before_reaping_leader(self):
        candidate = self.freeze_candidate()
        driver = mock.Mock(pid=123, args=['synthetic-driver'])
        previous = signal.getsignal(signal.SIGTERM)
        def cleanup(process):
            self.assertIs(process, driver)
            driver.wait.assert_not_called()
        with mock.patch.object(snapshot_tools.subprocess, 'Popen', return_value=driver), \
             mock.patch.object(snapshot_tools, 'wait_validation_driver'), \
             mock.patch.object(snapshot_tools, 'validation_descendants', return_value=[456]), \
             mock.patch.object(snapshot_tools, 'cleanup_validation_group', side_effect=cleanup) as clean:
            with self.assertRaisesRegex(RuntimeError, 'left descendants'):
                self.validate_candidate(candidate, 'pass')
            clean.assert_called_once_with(driver)
        self.assertIs(signal.getsignal(signal.SIGTERM), previous)
        self.assertFalse((self.store / 'current.json').exists())

    def test_validation_wrapper_sigterm_cleans_and_reaps_synthetic_driver(self):
        candidate = self.freeze_candidate()
        pidfile = self.root / 'driver.pid'
        script = Path(snapshot_tools.__file__).resolve()
        code = ("import os,pathlib,sys,time; pathlib.Path(sys.argv[1]).write_text(str(os.getpid())); "
                "time.sleep(30)")
        command = [sys.executable, str(script), 'validate', '--snapshot', str(candidate),
                   '--store', str(self.store), '--timeout', '10', '--',
                   sys.executable, '-c', code, str(pidfile)]
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as wrapper:
            deadline = time.monotonic() + 5
            while not pidfile.exists() and wrapper.poll() is None and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertTrue(pidfile.exists(), 'Synthetic driver did not start')
            wrapper.send_signal(signal.SIGTERM)
            stdout, stderr = wrapper.communicate(timeout=8)
        self.assertEqual(wrapper.returncode, 2, stderr)
        self.assertIn('interrupted by signal', stderr)
        self.assertEqual(stdout, '')
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pidfile.read_text()), 0)
        self.assertFalse((self.store / 'current.json').exists())

    def test_validation_cli_requires_timeout_and_command(self):
        candidate = self.freeze_candidate()
        script = Path(snapshot_tools.__file__).resolve()
        base = [sys.executable, str(script), 'validate', '--snapshot', str(candidate),
                '--store', str(self.store)]
        for extra in ([], ['--timeout', '2']):
            result = subprocess.run(base + extra, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
        self.assertFalse((self.store / 'current.json').exists())

    def test_external_validation_is_signal_free_and_promotes_once(self):
        candidate = self.freeze_candidate()
        with mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn, \
             mock.patch.object(snapshot_tools.os, 'killpg') as signals:
            ticket = snapshot_tools.begin_validation(self.store, candidate)
            self.assertEqual(ticket.candidate, candidate)
            self.assertEqual(ticket.digest, candidate.name.removeprefix('snapshot-'))
            self.assertEqual(snapshot_tools.finish_validation(ticket, accepted=True), candidate)
            with self.assertRaisesRegex(RuntimeError, 'unused in-process ticket'):
                snapshot_tools.finish_validation(ticket, accepted=True)
            with self.assertRaisesRegex(RuntimeError, 'unused in-process ticket'):
                snapshot_tools.finish_validation({'accepted': True}, accepted=True)
            spawn.assert_not_called()
            signals.assert_not_called()
        self.assertEqual(self.selected_snapshot(), candidate)

    def test_external_validation_rejection_and_nonliteral_true_consume_ticket(self):
        candidate = self.freeze_candidate()
        with mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn, \
             mock.patch.object(snapshot_tools.os, 'killpg') as signals:
            for accepted in (False, 'true', 1, None):
                with self.subTest(accepted=accepted):
                    ticket = snapshot_tools.begin_validation(self.store, candidate)
                    with self.assertRaisesRegex(RuntimeError, 'did not accept'):
                        snapshot_tools.finish_validation(ticket, accepted=accepted)
                    with self.assertRaisesRegex(RuntimeError, 'unused in-process ticket'):
                        snapshot_tools.finish_validation(ticket, accepted=True)
            spawn.assert_not_called()
            signals.assert_not_called()
        self.assertFalse((self.store / 'current.json').exists())

    def test_external_validation_mutation_cannot_promote_or_replay(self):
        candidate = self.freeze_candidate()
        with mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn, \
             mock.patch.object(snapshot_tools.os, 'killpg') as signals:
            ticket = snapshot_tools.begin_validation(self.store, candidate)
            target = candidate / 'config/filesystem.json'
            target.chmod(0o644)
            target.write_text('changed')
            with self.assertRaisesRegex(RuntimeError, 'inventory or checksum mismatch'):
                snapshot_tools.finish_validation(ticket, accepted=True)
            with self.assertRaisesRegex(RuntimeError, 'unused in-process ticket'):
                snapshot_tools.finish_validation(ticket, accepted=True)
            spawn.assert_not_called()
            signals.assert_not_called()
        self.assertFalse((self.store / 'current.json').exists())

    def test_external_validation_stale_lease_preserves_newer_selection(self):
        candidate = self.freeze_candidate()
        (self.source / 'config/filesystem.json').write_text('{"newer":true}')
        newer = self.freeze_candidate()
        with mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn, \
             mock.patch.object(snapshot_tools.os, 'killpg') as signals:
            ticket = snapshot_tools.begin_validation(self.store, candidate)
            self.promote_candidate(newer)
            with self.assertRaisesRegex(RuntimeError, 'refusing stale promotion'):
                snapshot_tools.finish_validation(ticket, accepted=True)
            spawn.assert_not_called()
            signals.assert_not_called()
        self.assertEqual(self.selected_snapshot(), newer)

    def test_external_validation_same_bytes_aba_is_stale(self):
        candidate = self.freeze_candidate()
        self.promote_candidate(candidate)
        with mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn, \
             mock.patch.object(snapshot_tools.os, 'killpg') as signals:
            ticket = snapshot_tools.begin_validation(self.store, candidate)
            before = (self.store / 'current.json').read_bytes()
            self.promote_candidate(candidate)
            self.assertEqual((self.store / 'current.json').read_bytes(), before)
            with self.assertRaisesRegex(RuntimeError, 'refusing stale promotion'):
                snapshot_tools.finish_validation(ticket, accepted=True)
            spawn.assert_not_called()
            signals.assert_not_called()

    def test_external_validation_refuses_bad_existing_pointer_at_begin(self):
        candidate = self.freeze_candidate()
        (self.store / 'current.json').write_text('invalid')
        with mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn, \
             mock.patch.object(snapshot_tools.os, 'killpg') as signals:
            with self.assertRaisesRegex(RuntimeError, 'Cannot read current'):
                snapshot_tools.begin_validation(self.store, candidate)
            spawn.assert_not_called()
            signals.assert_not_called()

    def test_external_validation_deadline_in_future_promotes_signal_free(self):
        candidate = self.freeze_candidate()
        ticket = snapshot_tools.begin_validation(self.store, candidate)
        with mock.patch.object(snapshot_tools.time, 'monotonic', return_value=10), \
             mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn, \
             mock.patch.object(snapshot_tools.os, 'killpg') as signals:
            self.assertEqual(snapshot_tools.finish_validation(
                ticket, accepted=True, absolute_monotonic_deadline=20), candidate)
            spawn.assert_not_called()
            signals.assert_not_called()
        self.assertEqual(self.selected_snapshot(), candidate)

    def test_external_validation_invalid_deadline_consumes_ticket(self):
        candidate = self.freeze_candidate()
        for value in (True, False, 0, -1, float('nan'), float('inf'), '20'):
            with self.subTest(deadline=value):
                ticket = snapshot_tools.begin_validation(self.store, candidate)
                with self.assertRaisesRegex(RuntimeError, 'finite and positive'):
                    snapshot_tools.finish_validation(
                        ticket, accepted=True, absolute_monotonic_deadline=value)
                with self.assertRaisesRegex(RuntimeError, 'unused in-process ticket'):
                    snapshot_tools.finish_validation(ticket, accepted=True)
        self.assertFalse((self.store / 'current.json').exists())

    def test_external_validation_expired_deadline_preserves_pointer(self):
        candidate = self.freeze_candidate()
        self.promote_candidate(candidate)
        pointer = self.store / 'current.json'
        before = snapshot_tools.current_lease(self.store)
        ticket = snapshot_tools.begin_validation(self.store, candidate)
        with mock.patch.object(snapshot_tools.time, 'monotonic', return_value=20):
            with self.assertRaisesRegex(RuntimeError, 'deadline expired'):
                snapshot_tools.finish_validation(
                    ticket, accepted=True, absolute_monotonic_deadline=20)
        self.assertEqual(snapshot_tools.current_lease(self.store), before)
        self.assertTrue(pointer.exists())
        with self.assertRaisesRegex(RuntimeError, 'unused in-process ticket'):
            snapshot_tools.finish_validation(ticket, accepted=True)

    def test_external_validation_final_verification_expiry_cannot_select(self):
        candidate = self.freeze_candidate()
        self.promote_candidate(candidate)
        before = snapshot_tools.current_lease(self.store)
        ticket = snapshot_tools.begin_validation(self.store, candidate)
        clock = [10]
        original = snapshot_tools.candidate_validation_identity
        def verify_then_expire(*args):
            identity = original(*args)
            clock[0] = 20
            return identity
        with mock.patch.object(snapshot_tools.time, 'monotonic', side_effect=lambda: clock[0]), \
             mock.patch.object(snapshot_tools, 'candidate_validation_identity', side_effect=verify_then_expire), \
             mock.patch.object(snapshot_tools.os, 'replace') as replace:
            with self.assertRaisesRegex(RuntimeError, 'deadline expired'):
                snapshot_tools.finish_validation(
                    ticket, accepted=True, absolute_monotonic_deadline=20)
            replace.assert_not_called()
        self.assertEqual(snapshot_tools.current_lease(self.store), before)
        self.assertFalse(list(self.store.glob('.current-*')))

    def test_external_validation_pointer_fsync_expiry_cannot_select(self):
        candidate = self.freeze_candidate()
        self.promote_candidate(candidate)
        before = snapshot_tools.current_lease(self.store)
        ticket = snapshot_tools.begin_validation(self.store, candidate)
        clock = [10]
        original = snapshot_tools.os.fsync
        def fsync_then_expire(fd):
            original(fd)
            clock[0] = 20
        with mock.patch.object(snapshot_tools.time, 'monotonic', side_effect=lambda: clock[0]), \
             mock.patch.object(snapshot_tools.os, 'fsync', side_effect=fsync_then_expire), \
             mock.patch.object(snapshot_tools.os, 'replace') as replace, \
             mock.patch.object(snapshot_tools.subprocess, 'Popen') as spawn, \
             mock.patch.object(snapshot_tools.os, 'killpg') as signals:
            with self.assertRaisesRegex(RuntimeError, 'deadline expired'):
                snapshot_tools.finish_validation(
                    ticket, accepted=True, absolute_monotonic_deadline=20)
            replace.assert_not_called()
            spawn.assert_not_called()
            signals.assert_not_called()
        self.assertEqual(snapshot_tools.current_lease(self.store), before)
        self.assertFalse(list(self.store.glob('.current-*')))
        with self.assertRaisesRegex(RuntimeError, 'unused in-process ticket'):
            snapshot_tools.finish_validation(ticket, accepted=True)

    def activate_nhart(self, selected=None):
        selected = selected or ['Mods/new-horizons/Images/icon.png']
        manifest = {'format': 1, 'requiredFamilies': ['fixture'],
                    'requiredFamilyCounts': {'fixture': len(selected)}, 'entries': []}
        for name in selected:
            payload = (self.source / name).read_bytes()
            resource = ('CONFIG/' if name.startswith('config/') else 'SPRITES/') + Path(name).name
            manifest['entries'].append({'source': name, 'resource': resource,
                'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(),
                'family': 'fixture', 'origin': 'synthetic test', 'selection': 'explicit fixture',
                'approval': 'test only'})
        self.art_manifest = self.root / 'expected-art.json'
        self.art_manifest.write_text(json.dumps(manifest))
        patcher = mock.patch.object(snapshot_tools, 'ART_MANIFEST', self.art_manifest)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.pack = self.source / snapshot_tools.RUNTIME_ART_PACK
        snapshot_tools.nhart.pack(manifest, self.source, self.pack)
        for name in selected:
            (self.source / name).unlink()
        (self.source / 'config/filesystem.json').write_text(json.dumps({'filesystem': {'': [
            {'type': 'nhart', 'path': snapshot_tools.RUNTIME_ART_PACK, 'overlay': True}]}}))
        (self.source / 'Mods/new-horizons/mod.json').write_text(json.dumps({'filesystem': {'': [
            {'type': 'dir', 'path': '/Content'}, {'type': 'nhart', 'path': '/NewHorizons.nhart'}]}}))

    def test_nhart_freeze_without_loose_images_and_source_link_support(self):
        self.activate_nhart()
        candidate = self.freeze_candidate()
        self.assertEqual((candidate / snapshot_tools.RUNTIME_ART_PACK).read_bytes(), self.pack.read_bytes())
        self.assertFalse((candidate / 'Mods/new-horizons/Images/icon.png').exists())
        verify_snapshot(candidate)

    def test_declared_nhart_missing_pack_cannot_use_legacy_fallback(self):
        self.activate_nhart()
        self.pack.unlink()
        (self.source / 'Mods/new-horizons/Images/icon.png').write_bytes(b'fallback')
        with self.assertRaisesRegex(RuntimeError, 'pack missing'):
            self.freeze_candidate()
        self.assertFalse(list(self.store.glob('snapshot-*')))

    def test_selected_packed_magic_config_is_authoritative_over_build_linked_copy(self):
        config = self.source / 'config/newHorizonsMagicAssets.json'
        config.write_text(json.dumps({'casting': {}, 'guildBooks': {'tower': {'image': 'selected.png'}}}))
        self.activate_nhart(['Mods/new-horizons/Images/icon.png', 'config/newHorizonsMagicAssets.json'])
        selected_pack = self.pack.read_bytes()
        # The linked checkout can retain an older authoring fragment, like the
        # real build's config link; the selected packed supporting file wins.
        config.write_text(json.dumps({'casting': {}}))
        candidate = self.freeze_candidate()
        self.assertFalse((candidate / 'config/newHorizonsMagicAssets.json').exists())
        self.assertEqual((candidate / snapshot_tools.RUNTIME_ART_PACK).read_bytes(), selected_pack)
        self.assertEqual(json.loads(config.read_text()), {'casting': {}})
        verify_snapshot(candidate)

    def test_unpacked_magic_config_is_not_silently_excluded(self):
        self.activate_nhart()
        config = self.source / 'config/newHorizonsMagicAssets.json'
        config.write_text(json.dumps({'casting': {'ordinary': True}}))
        candidate = self.freeze_candidate()
        self.assertEqual((candidate / 'config/newHorizonsMagicAssets.json').read_bytes(), config.read_bytes())
        verify_snapshot(candidate)

    def test_selected_magic_config_exclusion_requires_verified_pack_and_manifest(self):
        config = self.source / 'config/newHorizonsMagicAssets.json'
        config.write_text('{}')
        self.activate_nhart(['Mods/new-horizons/Images/icon.png', 'config/newHorizonsMagicAssets.json'])
        config.write_text('{"stale": true}')
        data = bytearray(self.pack.read_bytes())
        data[snapshot_tools.nhart.HEADER.size] ^= 1
        self.pack.write_bytes(data)
        with self.assertRaises(snapshot_tools.nhart.NHArtError):
            self.freeze_candidate()
        self.assertFalse(list(self.store.glob('snapshot-*')))

    def test_nhart_corruption_rejected_before_freeze(self):
        self.activate_nhart()
        data = bytearray(self.pack.read_bytes())
        data[snapshot_tools.nhart.HEADER.size] ^= 1
        self.pack.write_bytes(data)
        with self.assertRaises(snapshot_tools.nhart.NHArtError):
            self.freeze_candidate()

    def test_nhart_duplicate_loose_art_rejected(self):
        payload = (self.source / 'Mods/new-horizons/Images/icon.png').read_bytes()
        self.activate_nhart()
        (self.source / 'Mods/new-horizons/Images/icon.png').write_bytes(payload)
        with self.assertRaisesRegex(RuntimeError, 'still installed loose'):
            self.freeze_candidate()

    def test_loose_baseline_maps_selected_path_to_current_verified_pack(self):
        baseline = self.freeze_candidate()
        (self.source / 'Mods/new-horizons/Images/icon.png').write_bytes(b'deliberately updated art')
        self.activate_nhart()
        candidate = self.freeze_candidate(baseline)
        self.assertFalse((candidate / 'Mods/new-horizons/Images/icon.png').exists())
        self.assertEqual((candidate / snapshot_tools.RUNTIME_ART_PACK).read_bytes(), self.pack.read_bytes())
        verify_snapshot(candidate)

    def test_nhart_retention_does_not_waive_unmapped_old_art(self):
        old = self.source / 'Mods/new-horizons/Images/unselected-old.png'
        old.write_bytes(b'baseline-only art')
        baseline = self.freeze_candidate()
        old.unlink()
        self.activate_nhart()
        with self.assertRaisesRegex(RuntimeError, 'unselected-old.png'):
            self.freeze_candidate(baseline)

    def test_nhart_retention_never_waives_missing_gameplay_config(self):
        gameplay = self.source / 'config/optionalGameplay.json'
        gameplay.write_bytes(b'{"gameplay":true}')
        baseline = self.freeze_candidate()
        gameplay.unlink()
        self.activate_nhart()
        with self.assertRaisesRegex(RuntimeError, 'optionalGameplay.json'):
            self.freeze_candidate(baseline)

    def test_selected_art_configuration_and_casting_files_are_retained_inside_pack(self):
        art_config, frame = self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        selected = ['Mods/new-horizons/Images/icon.png',
                    art_config.relative_to(self.source).as_posix(),
                    frame.relative_to(self.source).as_posix()]
        self.activate_nhart(selected)
        candidate = self.freeze_candidate(baseline)
        self.assertFalse((candidate / selected[1]).exists())
        self.assertFalse((candidate / selected[2]).exists())
        self.assertTrue((candidate / snapshot_tools.RUNTIME_ART_PACK).is_file())

    def test_nhart_must_match_expected_current_manifest(self):
        self.activate_nhart()
        changed = json.loads(self.art_manifest.read_text())
        changed['entries'][0]['approval'] = 'different selected inventory'
        self.art_manifest.write_text(json.dumps(changed))
        with self.assertRaisesRegex(RuntimeError, 'expected source manifest'):
            self.freeze_candidate()

    def test_requires_explicit_snapshot_and_does_not_fall_back_to_build_bin(self):
        with self.assertRaisesRegex(RuntimeError, "No frozen Linux playable snapshot"):
            self.selected_snapshot()
        self.assertTrue(self.client.exists())

    def test_copy_is_immutable_from_source_edits_and_checksum_verified(self):
        snapshot = self.freeze_candidate()
        with self.assertRaisesRegex(RuntimeError, "No frozen Linux playable snapshot"):
            self.selected_snapshot()
        self.promote_candidate(snapshot)
        original = (snapshot / "config/filesystem.json").read_text()
        (self.source / "config/filesystem.json").write_text('{"active": true}')
        self.assertEqual((snapshot / "config/filesystem.json").read_text(), original)
        self.assertEqual(self.selected_snapshot(), snapshot)
        self.assertEqual(verify_snapshot(snapshot)["snapshot_sha256"], snapshot.name.removeprefix("snapshot-"))
        self.assertFalse((snapshot / "config").is_symlink())
        self.assertFalse((snapshot / "Mods/new-horizons").is_symlink())

        writable = snapshot / "config/filesystem.json"
        writable.chmod(0o644)
        writable.write_text("changed")
        with self.assertRaisesRegex(RuntimeError, "inventory or checksum mismatch"):
            self.selected_snapshot()

    def test_promotions_retain_every_snapshot_that_may_be_in_use(self):
        first = self.freeze_candidate()
        self.promote_candidate(first)
        unrelated = self.store / "operator-notes"
        unrelated.mkdir()
        (unrelated / "keep.txt").write_text("leave me")

        (self.source / "config/filesystem.json").write_text('{"candidate": 2}')
        second = self.freeze_candidate()
        self.assertNotEqual(first, second)
        self.assertEqual(self.selected_snapshot(), first)
        self.assertTrue(first.is_dir())
        self.promote_candidate(second)
        self.assertEqual(self.selected_snapshot(), second)

        (self.source / "config/filesystem.json").write_text('{"candidate": 3}')
        third = self.freeze_candidate()
        self.assertNotEqual(second, third)
        self.assertEqual(self.selected_snapshot(), second)
        self.promote_candidate(third)
        self.assertEqual(self.selected_snapshot(), third)
        self.promote_candidate(third)
        snapshots = sorted(path for path in self.store.glob("snapshot-*") if path.is_dir())
        self.assertEqual(set(snapshots), {first, second, third})
        pointer = json.loads((self.store / "current.json").read_text())
        self.assertEqual(pointer["previous_snapshot"], second.name)
        self.assertEqual((unrelated / "keep.txt").read_text(), "leave me")

    def test_nested_source_symlink_is_rejected(self):
        link = self.source / "config" / "elsewhere"
        link.symlink_to(self.root / "outside", target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "symlink or non-regular file"):
            self.freeze_candidate()

    def test_current_pointer_is_atomic_json_identity(self):
        snapshot = self.freeze_candidate()
        self.promote_candidate(snapshot)
        pointer = json.loads((self.store / "current.json").read_text())
        self.assertEqual(pointer["snapshot"], snapshot.name)
        self.assertEqual(pointer["snapshot_sha256"], snapshot.name.removeprefix("snapshot-"))
        self.assertFalse((self.store / "current.json").is_symlink())

    def test_corrupt_candidate_cannot_be_promoted(self):
        snapshot = self.freeze_candidate()
        with self.assertRaisesRegex(RuntimeError, "inventory or checksum mismatch"):
            file = snapshot / "config/filesystem.json"
            file.chmod(0o644)
            file.write_text("tampered")
            self.promote_candidate(snapshot)
        with self.assertRaisesRegex(RuntimeError, "No frozen Linux playable snapshot"):
            self.selected_snapshot()

    def add_optional_magic_resources(self):
        manifest = self.source / "config/newHorizonsMagicAssets.json"
        image = self.source / "Mods/new-horizons/Images/NH_magic_assets/casting/nature/frame.png"
        image.parent.mkdir(parents=True)
        manifest.write_text('{"casting": "NH_magic_assets/casting/nature/frame.png"}')
        image.write_bytes(b"synthetic optional casting art")
        return manifest, image

    def test_retention_blocks_optional_manifest_and_art_omissions_before_copy(self):
        manifest, image = self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        self.promote_candidate(baseline)
        manifest.unlink()
        image.unlink()
        with self.assertRaisesRegex(RuntimeError, "omits retained baseline resources") as failure:
            self.freeze_candidate(baseline)
        self.assertIn("config/newHorizonsMagicAssets.json", str(failure.exception))
        self.assertIn("Images/NH_magic_assets/casting/nature/frame.png", str(failure.exception))
        self.assertEqual(self.selected_snapshot(), baseline)
        self.assertEqual(list(self.store.glob("snapshot-*")), [baseline])
        self.assertFalse(list(self.store.glob(".snapshot-*")))
        self.assertFalse(manifest.exists(), "Guard must not auto-copy old configuration")
        self.assertFalse(image.exists(), "Guard must not auto-copy old artwork")

    def test_retention_allows_updated_gameplay_and_new_binaries_at_existing_paths(self):
        self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        changed = self.source / "config/newHorizonsMagic.json"
        changed.write_text('{"updatedGameplay": true}')
        self.client.write_text("#!/bin/sh\nexit 1\n")
        (self.bin / "libvcmi.so").write_bytes(b"new matching library")
        candidate = self.freeze_candidate(baseline)
        self.assertNotEqual(candidate, baseline)
        self.assertEqual((candidate / "config/newHorizonsMagic.json").read_text(), changed.read_text())
        self.assertEqual((candidate / "new-horizons").read_text(), self.client.read_text())
        self.assertEqual((candidate / "libvcmi.so").read_bytes(), b"new matching library")
        verify_snapshot(candidate)

    def test_new_snapshot_uses_actual_branded_client_name(self):
        candidate = self.freeze_candidate()
        self.assertEqual(snapshot_client(candidate), candidate / 'new-horizons')
        self.assertFalse((candidate / 'vcmiclient').exists())

    def test_legacy_immutable_snapshot_client_remains_unchanged(self):
        candidate = self.freeze_candidate()
        make_removable(candidate)
        (candidate / 'new-horizons').rename(candidate / 'vcmiclient')
        digest = write_metadata(candidate, package_files(candidate))
        legacy = self.store / ('snapshot-' + digest)
        candidate.rename(legacy)
        before = (legacy / 'SNAPSHOT.json').read_bytes()
        self.promote_candidate(legacy)
        self.assertEqual(snapshot_client(legacy), legacy / 'vcmiclient')
        self.assertEqual(self.selected_snapshot(), legacy)
        self.assertEqual((legacy / 'SNAPSHOT.json').read_bytes(), before)

    def test_new_freeze_does_not_rename_legacy_executable_silently(self):
        old = self.client.with_name('vcmiclient')
        self.client.rename(old)
        self.client = old
        with self.assertRaisesRegex(RuntimeError, 'actual new-horizons executable'):
            self.freeze_candidate()

    def test_verified_snapshot_with_ambiguous_client_names_is_refused(self):
        candidate = self.freeze_candidate()
        make_removable(candidate)
        legacy = candidate / 'vcmiclient'
        legacy.write_bytes((candidate / 'new-horizons').read_bytes())
        legacy.chmod(0o755)
        digest = write_metadata(candidate, package_files(candidate))
        renamed = self.store / ('snapshot-' + digest)
        candidate.rename(renamed)
        with self.assertRaisesRegex(RuntimeError, 'exactly one current or legacy client'):
            snapshot_client(renamed)

    def test_retention_rejects_tampered_baseline(self):
        baseline = self.freeze_candidate()
        image = baseline / "Mods/new-horizons/Images/icon.png"
        image.chmod(0o644)
        image.write_bytes(b"tampered baseline")
        with self.assertRaisesRegex(RuntimeError, "inventory or checksum mismatch"):
            self.freeze_candidate(baseline)
        self.assertFalse(list(self.store.glob(".snapshot-*")))

    def test_retention_rejects_baseline_symlink(self):
        baseline = self.freeze_candidate()
        link = self.root / "baseline-link"
        link.symlink_to(baseline, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "missing or is a symlink"):
            self.freeze_candidate(link)

    def test_retention_checks_only_curated_resource_roots(self):
        baseline = self.freeze_candidate()
        make_removable(baseline)
        (baseline / "operator-notes.txt").write_text("Verified baseline note, not a resource")
        digest = write_metadata(baseline, package_files(baseline))
        renamed = self.store / ("snapshot-" + digest)
        baseline.rename(renamed)
        verify_snapshot(renamed)
        candidate = self.freeze_candidate(renamed)
        self.assertFalse((candidate / "operator-notes.txt").exists())
        verify_snapshot(candidate)

    def test_without_retention_option_preserves_existing_omission_behavior(self):
        manifest, image = self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        manifest.unlink()
        image.unlink()
        candidate = self.freeze_candidate()
        self.assertNotEqual(candidate, baseline)
        self.assertFalse((candidate / "config/newHorizonsMagicAssets.json").exists())
        verify_snapshot(candidate)

    def test_cli_retention_option_reports_missing_optional_resources(self):
        manifest, _ = self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        manifest.unlink()
        script = Path(__file__).resolve().parents[1] / "ci/linux_playable_snapshot.py"
        result = subprocess.run([sys.executable, str(script), "freeze", "--client", str(self.client),
                                 "--resources", str(self.resources), "--store", str(self.store),
                                 "--retain-resources-from", str(baseline)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("config/newHorizonsMagicAssets.json", result.stderr)


if __name__ == "__main__":
    unittest.main()
