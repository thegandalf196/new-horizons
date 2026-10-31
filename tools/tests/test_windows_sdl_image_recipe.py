"""Windows decoder selection and legacy source-repair contracts; no processes."""
from pathlib import Path
import ast
import sys
import types
import unittest
from unittest import mock
import yaml
from test_sdl_image_conan_recipe import imported

ROOT = Path(__file__).resolve().parents[2]


class WindowsDecoderRecipeTest(unittest.TestCase):
    def test_local_mingw_selects_and_exports_the_same_decoder(self):
        tree = ast.parse((ROOT/'tools/ci/conan_mingw_client.py').read_text())
        consumer = next(node for node in tree.body if isinstance(node, ast.ClassDef))
        declaration = next(node for node in consumer.body if isinstance(node, ast.Assign)
                           and any(isinstance(target, ast.Name) and target.id == 'requires'
                                   for target in node.targets))
        requirements = ast.literal_eval(declaration.value)
        self.assertEqual([value for value in requirements if value.startswith('sdl_image/')],
                         ['sdl_image/3.4.4@newhorizons/patched'])
        script = (ROOT/'tools/ci/build_mingw_client.sh').read_text()
        export = 'conan export "$repo/tools/conan/sdl_image"'
        self.assertIn(export, script)
        self.assertLess(script.index(export), script.index('conan install tools/ci/conan_mingw_client.py'))
        export_block = script[script.index(export):script.index('conan install tools/ci/conan_mingw_client.py')]
        self.assertIn('--version=3.4.4 --user=newhorizons --channel=patched', export_block)
        self.assertIn('-cc "core.cache:storage_path=$package_storage"', export_block)
        self.assertIn('[[ "$stage" != generate ]] || build_policy=never', script)

    def test_notice_consumer_preserves_upstream_requirements_without_mutating_default(self):
        class Base:
            requires = ['zlib/1', 'sdl_image/[^3.4.4]', 'sdl_mixer/3']
            requirements = mock.Mock()
        dependency = types.ModuleType('dependencies.conanfile')
        dependency.VCMI = Base
        with mock.patch.object(sys, 'path', list(sys.path)):
            module = imported(ROOT/'tools/ci/conan_notice_repack.py', 'notice_repack_control',
                              {'dependencies.conanfile': dependency})
        self.assertIs(module.NoticeRepack.requires, Base.requires)
        self.assertIs(module.NoticeRepack.requirements, Base.requirements)
        self.assertIn('sdl_image/3.4.4@newhorizons/patched', module.root_recipe.VCMIApp.requires)
        self.assertEqual(Base.requires, ['zlib/1', 'sdl_image/[^3.4.4]', 'sdl_mixer/3'])

    def test_workflow_separates_fresh_decoder_build_and_notice_only_repair(self):
        workflow = yaml.safe_load((ROOT/'.github/workflows/new-horizons-windows.yml').read_text())
        steps = [step for job in workflow['jobs'].values() for step in job['steps']]
        resolution = next(step for step in steps if step.get('name') == 'Resolve upstream dependency profile')
        script = resolution['run']
        self.assertIn("build_policy=(--build='sdl_image/3.4.4@newhorizons/patched' --conf='tools.build:jobs=4')", script)
        self.assertIn('install_recipe=tools/ci/conan_notice_repack.py\n', script)
        self.assertIn('build_policy=(--build=never)\n', script)
        self.assertIn('conan export tools/conan/sdl_image --version=3.4.4 --user=newhorizons --channel=patched', script)
        self.assertIn('conan install "$install_recipe"', script)
        self.assertIn('"${build_policy[@]}"', script)
        self.assertNotIn('--build=missing', script)
        self.assertNotIn('--build=cascade', script)
        self.assertEqual(resolution['env']['ORIGINAL_REPACK_RUN_ID'], '${{ inputs.repack_run_id }}')
        self.assertIn('without compiling the client', workflow.get('on', workflow.get(True))['workflow_dispatch']['inputs']['preflight_only']['description'])
        self.assertTrue(all(job['env']['PYTHONDONTWRITEBYTECODE'] == '1' for job in workflow['jobs'].values()))

    def test_shipping_decoder_gate_requires_pixels_and_real_warning_absence(self):
        workflow = yaml.safe_load((ROOT/'.github/workflows/new-horizons-windows.yml').read_text())
        steps = [step for job in workflow['jobs'].values() for step in job['steps']]
        gate = next(step for step in steps if step.get('name') == 'Verify selected masks and palette updates with shipping SDL3 (CPU only)')
        self.assertIn('--interlace-regression', gate['run'])
        self.assertIn('Interlace handling should be turned on when using png_read_image', gate['run'])
        self.assertIn('sdl3-mask.stderr.log', gate['run'])
        self.assertIn('all 81 exact RGBA pixels', gate['run'])
        self.assertIn("inputs.repack_run_id == ''", gate['if'])
        retention = next(step for step in steps if step.get('name') == 'Retain decoder pixel and warning evidence')
        self.assertIn('always()', retention['if'])
        self.assertEqual(retention['with']['path'], 'build/new-horizons-windows-x64/sdl3-mask.*.log')
        self.assertIn('/tools/ci/patches/sdl3-image-3.4.4-interlace.patch text eol=lf',
                      (ROOT/'.gitattributes').read_text())

    def test_official_sdl_source_seed_is_pinned_and_bound_before_dependency_build(self):
        workflow = yaml.safe_load((ROOT/'.github/workflows/new-horizons-windows.yml').read_text())
        steps = [step for job in workflow['jobs'].values() for step in job['steps']]
        seed = next(step for step in steps if step.get('name') == 'Seed verified Conan source cache fallback for SDL3_image')
        resolution = next(step for step in steps if step.get('name') == 'Resolve upstream dependency profile')
        expected = '29751304a13d25ac513f24305fa25b06a6edd9607718c90129b8350d35fc5573'
        self.assertLess(steps.index(seed), steps.index(resolution))
        self.assertEqual(seed['env']['SDL_IMAGE_SOURCE_SHA256'], expected)
        self.assertEqual(seed['env']['SDL_IMAGE_SOURCE_BYTES'], 11123077)
        script = seed['run']
        self.assertIn('https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.4/SDL3_image-3.4.4.tar.gz', script)
        self.assertIn('curl --fail --location', script)
        self.assertIn("--proto '=https' --proto-redir '=https'", script)
        self.assertIn('--max-time 120 --max-filesize 15728640 --retry 0', script)
        self.assertIn('archive.stat().st_size != int(sys.argv[4])', script)
        self.assertIn('seed_source_cache(Path(sys.argv[1]), archive, sys.argv[3])', script)
        self.assertIn('echo "NH_CONAN_SOURCE_CACHE=$source_cache" >> "$GITHUB_ENV"', script)
        self.assertEqual(script.count('curl --fail'), 1)
        self.assertNotIn('for url', script)
        self.assertNotIn('--insecure', script)
        binding = '--core-conf="core.sources:download_cache=$NH_CONAN_SOURCE_CACHE"'
        self.assertEqual(resolution['run'].count(binding), 1)
        self.assertIn('conan install "$install_recipe"', resolution['run'])
        # Transport/cache fallback must not replace the recipe's canonical source
        # or weaken the independently maintained source and shipping pixel gates.
        data = yaml.safe_load((ROOT/'tools/conan/sdl_image/conandata.yml').read_text())
        self.assertEqual(data['sources']['3.4.4'], {
            'sha256': expected,
            'url': 'https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.4/SDL3_image-3.4.4.tar.gz'})
        self.assertTrue(any('preflight_dependency_sources' in step.get('run', '')
                            and 'source_cache=pathlib.Path(os.environ[\'NH_CONAN_SOURCE_CACHE\'])' in step.get('run', '')
                            for step in steps))


if __name__ == '__main__':
    unittest.main()
