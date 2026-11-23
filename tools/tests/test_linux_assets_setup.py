"""Noninteractive setup contracts; synthetic originals and no game/SDL execution."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import linux_assets_setup as setup


class LinuxAssetsSetupTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nh setup ü ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / 'package'
        self.package.mkdir()
        for name in ('new-horizons', 'new-horizons-launch.sh', setup.PICKER_NAME):
            path = self.package / name
            path.write_text('synthetic never executed')
            path.chmod(0o755)
        self.assets = self.root / 'Complete ü with spaces'
        for name in ('Data', 'Maps', 'Mp3'):
            (self.assets / name).mkdir(parents=True)
        for name in setup.ARCHIVES:
            (self.assets / 'Data' / name).write_bytes(b'synthetic archive')
        self.profile = self.root / 'profile'

    def args(self, *extra):
        return setup.parse_args(['--launcher', str(self.package / 'new-horizons-launch.sh'),
                                 '--client', str(self.package / 'new-horizons'),
                                 '--profile', str(self.profile), *extra])

    def test_complete_validation_accepts_case_and_rejects_empty_archive(self):
        archive = self.assets / 'Data' / setup.ARCHIVES[0]
        renamed = archive.with_name(archive.name.upper())
        archive.rename(renamed)
        self.assertEqual(setup.validate_assets(self.assets), self.assets)
        renamed.write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'empty'):
            setup.validate_assets(self.assets)

    def test_private_atomic_persistence_does_not_create_profile(self):
        setup.write_selection(self.profile, self.assets)
        self.assertEqual(setup.read_selection(self.profile), str(self.assets))
        self.assertFalse(self.profile.exists())
        self.assertEqual(setup.state_file(self.profile).stat().st_mode & 0o777, 0o600)
        self.assertEqual(setup.state_file(self.profile).parent.stat().st_mode & 0o777, 0o700)
        with mock.patch.object(setup.os, 'replace', side_effect=OSError('failed write')):
            with self.assertRaises(OSError):
                setup.write_selection(self.profile, self.root / 'other')
        self.assertEqual(setup.read_selection(self.profile), str(self.assets))
        self.assertEqual(len(list(setup.state_file(self.profile).parent.iterdir())), 1)

    def test_symlink_or_public_state_refused(self):
        target = self.root / 'target'
        target.mkdir()
        setup.state_file(self.profile).parent.symlink_to(target)
        with self.assertRaises(ValueError):
            setup.read_selection(self.profile)
        setup.state_file(self.profile).parent.unlink()
        setup.write_selection(self.profile, self.assets)
        setup.state_file(self.profile).chmod(0o644)
        with self.assertRaises(ValueError):
            setup.read_selection(self.profile)

    def test_invalid_explicit_source_never_prompts_or_writes(self):
        with mock.patch.object(setup, 'choose') as choose:
            with self.assertRaises(OSError):
                setup.run(self.args('--assets', str(self.root / 'missing')))
        choose.assert_not_called()
        self.assertFalse(setup.state_file(self.profile).exists())

    def test_cancel_does_not_create_any_profile_state(self):
        with mock.patch.object(setup, 'choose', return_value=None):
            self.assertEqual(setup.run(self.args()), 0)
        self.assertFalse(self.profile.exists())
        self.assertFalse(setup.state_file(self.profile).parent.exists())

    def test_headless_and_verify_without_selection_never_prompt(self):
        for flags in (['--verify-only'], ['--noninteractive'], ['--', '--headless']):
            with self.subTest(flags=flags), mock.patch.object(setup, 'choose') as choose:
                with self.assertRaisesRegex(ValueError, 'No valid'):
                    setup.run(self.args(*flags))
                choose.assert_not_called()

    def test_verify_explicit_is_readonly_and_preserves_arguments(self):
        with mock.patch.object(setup.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run:
            self.assertEqual(setup.run(self.args('--assets', str(self.assets), '--verify-only')), 0)
        self.assertIn(str(self.assets), run.call_args.args[0])
        self.assertFalse(setup.state_file(self.profile).parent.exists())

    def test_saved_choice_launch_and_client_arguments_are_verbatim(self):
        setup.write_selection(self.profile, self.assets)
        args = self.args('--', '--testmap', 'Maps/a ü map.h3m', '')
        with mock.patch.object(setup, 'choose') as choose, \
                mock.patch.object(setup.subprocess, 'run', return_value=mock.Mock(returncode=0)), \
                mock.patch.object(setup.os, 'execv') as execute:
            setup.run(args)
        choose.assert_not_called()
        self.assertEqual(execute.call_args.args[1][-4:], ['--', '--testmap', 'Maps/a ü map.h3m', ''])

    def stable_saves(self):
        saves = self.root / setup.STABLE_PROFILE / setup.SAVES
        (saves / 'campaign').mkdir(parents=True)
        (saves / 'first.vsgm1').write_bytes(b'save one')
        (saves / 'campaign/second.vsgm1').write_bytes(b'save two')
        (saves / 'linked.vsgm1').symlink_to(saves / 'first.vsgm1')
        return saves

    def test_first_beta_start_copies_the_stable_saves_once(self):
        saves = self.stable_saves()
        before = {path: path.read_bytes() for path in saves.rglob('*') if path.is_file() and not path.is_symlink()}
        beta = self.root / setup.BETA_PROFILE
        self.profile = beta
        with mock.patch.object(setup.subprocess, 'run', return_value=mock.Mock(returncode=0)), \
                mock.patch.object(setup.os, 'execv') as execute, mock.patch('sys.stderr'):
            setup.run(self.args('--assets', str(self.assets), '--setup-only'))
            self.assertFalse(beta.exists())
            setup.run(self.args('--assets', str(self.assets)))
        execute.assert_called_once()
        copied = beta / setup.SAVES
        self.assertEqual((copied / 'first.vsgm1').read_bytes(), b'save one')
        self.assertEqual((copied / 'campaign/second.vsgm1').read_bytes(), b'save two')
        self.assertFalse(os.path.lexists(copied / 'linked.vsgm1'))
        # The managed launcher admits the seeded profile by its marker.
        self.assertEqual((beta / '.nh-profile').read_text(), setup.PROFILE_MARKER)
        self.assertEqual({path: path.read_bytes() for path in before}, before)
        self.assertEqual([path.name for path in self.root.glob('.' + setup.BETA_PROFILE + '*')], [])
        # Later starts keep the beta's own saves.
        (saves / 'first.vsgm1').write_bytes(b'changed in stable')
        setup.seed_beta_profile(beta)
        self.assertEqual((copied / 'first.vsgm1').read_bytes(), b'save one')

    def test_only_a_new_beta_profile_is_seeded_and_a_failed_copy_leaves_nothing(self):
        saves = self.stable_saves()
        setup.seed_beta_profile(self.profile)
        setup.seed_beta_profile(self.root / setup.STABLE_PROFILE)
        self.assertFalse(self.profile.exists())
        (saves / 'first.vsgm1').chmod(0)
        self.addCleanup((saves / 'first.vsgm1').chmod, 0o600)
        beta = self.root / setup.BETA_PROFILE
        if os.access(saves / 'first.vsgm1', os.R_OK):
            self.skipTest('Unreadable files are readable to this user')
        with self.assertRaisesRegex(OSError, 'Could not copy your saves to the beta'):
            setup.seed_beta_profile(beta)
        self.assertFalse(beta.exists())
        self.assertEqual([path.name for path in self.root.glob('.' + setup.BETA_PROFILE + '*')], [])
        empty = self.root / 'empty'
        empty.mkdir()
        setup.seed_beta_profile(empty / setup.BETA_PROFILE)
        self.assertEqual(list(empty.iterdir()), [])

    def test_moved_selection_prompts_and_preflight_failure_preserves_previous(self):
        old = self.root / 'moved originals'
        setup.write_selection(self.profile, old)
        with mock.patch.object(setup, 'choose', return_value=self.assets) as choose, \
                mock.patch.object(setup.subprocess, 'run', return_value=mock.Mock(returncode=17)):
            self.assertEqual(setup.run(self.args()), 17)
        choose.assert_called_once()
        self.assertEqual(setup.read_selection(self.profile), str(old))

    def test_overlapping_profile_refused_before_write(self):
        args = self.args('--assets', str(self.assets), '--setup-only', '--profile', str(self.assets / 'profile'))
        with self.assertRaisesRegex(ValueError, 'overlap'), mock.patch.object(setup.subprocess, 'run') as run:
            setup.run(args)
        run.assert_not_called()
        self.assertFalse((self.assets / 'profile.setup').exists())

    def test_invalid_picker_result_retries_without_persisting(self):
        result = lambda value: mock.Mock(returncode=0, stdout=os.fsencode(value) + b'\0')
        with mock.patch.dict(os.environ, {'DISPLAY': ':synthetic'}), \
                mock.patch.object(setup, 'notify') as notify, \
                mock.patch.object(setup.subprocess, 'run', side_effect=[result(self.root / 'missing'), result(self.assets)]):
            self.assertEqual(setup.choose(self.args(), None), self.assets)
        self.assertEqual(notify.call_count, 2)
        self.assertFalse(setup.state_file(self.profile).exists())


if __name__ == '__main__':
    unittest.main()
