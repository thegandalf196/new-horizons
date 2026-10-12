# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic recipe adapters/consumer controls; no Conan/cache/build/processes."""
import ast
import difflib
import hashlib
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
RECIPE = ROOT / 'tools/conan/sdl_image'
CHECKER = ROOT / 'tools/ci/prepare_sdl3_image_interlace.py'
PATCH = ROOT / 'tools/ci/patches/sdl3-image-3.4.4-interlace.patch'


class ConfigurationError(Exception):
    pass


def imported(path, name, extra=None):
    modules = {}
    for key, attributes in {
        'conan': {'ConanFile': object},
        'conan.errors': {'ConanInvalidConfiguration': ConfigurationError},
        'conan.tools.cmake': dict.fromkeys(('CMake','CMakeDeps','CMakeToolchain','cmake_layout'), mock.Mock()),
        'conan.tools.files': dict.fromkeys(('copy','get','patch','rmdir','save'), mock.Mock()),
        'conan.tools.scm': {'Version': mock.Mock()},
    }.items():
        module = types.ModuleType(key)
        module.__dict__.update(attributes)
        modules[key] = module
    modules.update(extra or {})
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


class RecipeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.module = imported(RECIPE/'conanfile.py','synthetic_sdl_recipe')
        self.recipe = self.module.SDLImageConan()
        self.recipe.recipe_folder = str(RECIPE)
        self.recipe.export_sources_folder = str(self.root/'exports')
        self.recipe.source_folder = str(self.root/'source')
        self.recipe.version = '3.4.4'
        self.recipe.conan_data = {'sources':{'3.4.4':{
            'url':'https://synthetic.invalid/source.tar.gz','sha256':'a'*64}}}
        (self.root/'exports/patches').mkdir(parents=True)
        (self.root/'source/src').mkdir(parents=True)

    def test_export_copies_only_existing_maintained_checker_and_patch(self):
        with mock.patch.object(self.module.shutil,'copyfile',wraps=shutil.copyfile) as copy:
            self.recipe.export_sources()
        self.assertEqual(copy.call_count,2)
        files = {p.relative_to(self.root/'exports').as_posix():p.read_bytes()
                 for p in (self.root/'exports').rglob('*') if p.is_file()}
        self.assertEqual(files,{'prepare_sdl3_image_interlace.py':CHECKER.read_bytes(),
            'patches/sdl3-image-3.4.4-interlace.patch':PATCH.read_bytes()})

    def test_standalone_collector_export_layout_is_supported(self):
        recipe = self.root/'standalone'
        (recipe/'patches').mkdir(parents=True)
        shutil.copyfile(CHECKER,recipe/CHECKER.name)
        shutil.copyfile(PATCH,recipe/'patches'/PATCH.name)
        self.recipe.recipe_folder = str(recipe)
        self.recipe.export_sources()
        self.assertEqual((self.root/'exports'/CHECKER.name).read_bytes(),CHECKER.read_bytes())

    def test_missing_standalone_exports_refused(self):
        self.recipe.recipe_folder = str(self.root/'missing/recipe')
        with self.assertRaises(ConfigurationError):
            self.recipe.export_sources()

    def source_fixture(self):
        source = (b'/* Synthetic implementation license */\n'
            b'int (*png_set_interlace_handling)(void *);\n'
            b'FUNCTION_LOADER_LIBPNG(png_set_interlace_handling, fake)\n'
            b'    lib.png_read_update_info(vars->png_ptr, vars->info_ptr);\n'
            b'    lib.png_read_update_info(context->png_ptr, context->info_ptr);\n')
        cmake = b'set(MAJOR_VERSION 3)\nset(MINOR_VERSION 4)\nset(MICRO_VERSION 4)\n'
        result = source
        for receiver in (b'vars',b'context'):
            marker = b'    lib.png_read_update_info('+receiver+b'->png_ptr, '+receiver+b'->info_ptr);'
            result = result.replace(marker,b'    lib.png_set_interlace_handling('+receiver+b'->png_ptr);\n'+marker)
        selected_patch = ''.join(difflib.unified_diff(source.decode().splitlines(True),
            result.decode().splitlines(True),fromfile='a/src/IMG_libpng.c',tofile='b/src/IMG_libpng.c')).encode()
        exported = self.root/'exports'
        (exported/'patches'/PATCH.name).write_bytes(selected_patch)
        # Test-only hash pins let the unchanged real checker validate fixtures.
        (exported/CHECKER.name).write_text(CHECKER.read_text()+
            '\nSOURCE_SHA256 = '+repr(hashlib.sha256(source).hexdigest())+
            '\nCMAKE_SHA256 = '+repr(hashlib.sha256(cmake).hexdigest())+'\n')
        def get(recipe, **kwargs):
            self.assertEqual(kwargs,{**recipe.conan_data['sources']['3.4.4'],'strip_root':True})
            (self.root/'source/src/IMG_libpng.c').write_bytes(source)
            (self.root/'source/CMakeLists.txt').write_bytes(cmake)
        def patch(recipe, **kwargs):
            self.assertEqual(kwargs,dict(base_path=recipe.source_folder,
                patch_file=str(exported/'patches'/PATCH.name),strip=1))
            (self.root/'source/src/IMG_libpng.c').write_bytes(result)
        return get, patch, source, result

    def test_source_checks_applies_and_verifies_exact_result_before_cleanup(self):
        get, patch, source, result = self.source_fixture()
        with mock.patch.object(self.module,'get',side_effect=get), \
             mock.patch.object(self.module,'patch',side_effect=patch) as apply, \
             mock.patch.object(self.module,'rmdir') as remove:
            self.recipe.source()
        self.assertEqual(apply.call_count,1)
        remove.assert_called_once_with(self.recipe,str(self.root/'source/external'))
        self.assertEqual((self.root/'source/src/IMG_libpng.c').read_bytes(),result)
        self.assertEqual(result.count(b'lib.png_set_interlace_handling('),2)

    def test_wrong_version_refused_before_download(self):
        self.recipe.version = '3.4.5'
        with mock.patch.object(self.module,'get') as get, self.assertRaises(ConfigurationError):
            self.recipe.source()
        get.assert_not_called()

    def test_actual_checker_execution_never_writes_bytecode_or_changes_global_flag(self):
        get, patch, source, result = self.source_fixture()
        before = {p.relative_to(self.root/'exports').as_posix():p.read_bytes()
                  for p in (self.root/'exports').rglob('*') if p.is_file()}
        with mock.patch.object(sys,'dont_write_bytecode',False), \
             mock.patch.object(self.module,'get',side_effect=get), \
             mock.patch.object(self.module,'patch',side_effect=patch), \
             mock.patch.object(self.module,'rmdir'):
            self.recipe.source()
            self.assertFalse(sys.dont_write_bytecode)
        self.assertEqual({p.relative_to(self.root/'exports').as_posix():p.read_bytes()
                          for p in (self.root/'exports').rglob('*') if p.is_file()},before)
        self.assertEqual((self.root/'source/src/IMG_libpng.c').read_bytes(),result)
        self.assertFalse(list(self.root.rglob('__pycache__')))
        self.assertFalse(list(self.root.rglob('*.pyc')))

    def test_wrong_source_refused_before_patch(self):
        get, patch, source, result = self.source_fixture()
        def changed(recipe, **kwargs):
            get(recipe,**kwargs)
            (self.root/'source/src/IMG_libpng.c').write_bytes(source+b'changed')
        with mock.patch.object(self.module,'get',side_effect=changed), \
             mock.patch.object(self.module,'patch') as apply, self.assertRaises(RuntimeError):
            self.recipe.source()
        apply.assert_not_called()

    def test_wrong_patch_refused_before_application(self):
        get, patch, source, result = self.source_fixture()
        (self.root/'exports/patches'/PATCH.name).write_bytes(b'wrong patch')
        with mock.patch.object(self.module,'get',side_effect=get), \
             mock.patch.object(self.module,'patch') as apply, self.assertRaises(RuntimeError):
            self.recipe.source()
        apply.assert_not_called()

    def test_wrong_applied_result_refused_before_cleanup(self):
        get, patch, source, result = self.source_fixture()
        with mock.patch.object(self.module,'get',side_effect=get), \
             mock.patch.object(self.module,'patch'), mock.patch.object(self.module,'rmdir') as remove, \
             self.assertRaises(ConfigurationError):
            self.recipe.source()
        remove.assert_not_called()

    def test_upstream_build_methods_source_unchanged(self):
        # Independently derived from the preserved ORIGINAL CCI recipe bytes:
        # SHA256 e7ae89c80dd7d42149a3e9c44cee1dc167cdf07eed5fb557d146ba2a6d10a8f6.
        # Hash exact method source after LF normalization, not ast.dump:
        # Python versions can add AST fields/change dump formatting.
        expected = {
            'layout':'cdffa3633a455a4d56919730fd8434e0dfaf025d98e037e333f50035c5a4ca4c',
            'requirements':'476973d6d16e0bbee3f834ea2604167618bc0039e351b709daed80866021d175',
            'validate':'7f182eec68f722c4fcceed88f817a6dc98e1ee56124815961c977e89245d3da6',
            'build_requirements':'b1716ec214567bb53b8d2abd7056fdf1626958059c8829c139bb5175620a15c2',
            'generate':'0b5cb5db1ca6c395b367f3de8a95c7882ac956fe3e1b7800db06f50321d523da',
            'build':'95d7310c8240994c8dab0bc74fc7a27e7304073f4472e9030cc38d543bb6c0b2',
            'package':'e56d7a567e6d8bd1e674b8cd5c0066a5e2e8273dc61c2ca6e9c2ea90d47ccbbe',
            'package_info':'1701ab987c915c5aa50c6e29d335b1ccb3829b9847e59a4c91f479d2d0e85ad4'}
        source = (RECIPE/'conanfile.py').read_bytes().decode('utf-8')
        for variant in (source, source.replace('\r\n','\n').replace('\n','\r\n')):
            with self.subTest(newlines='CRLF' if '\r\n' in variant else 'LF'):
                normalized = variant.replace('\r\n','\n').replace('\r','\n')
                cls = next(n for n in ast.parse(normalized).body if isinstance(n,ast.ClassDef))
                actual = {n.name:hashlib.sha256(ast.get_source_segment(normalized,n).encode('utf-8')).hexdigest()
                          for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in expected}
                self.assertEqual(actual,expected)

    def test_root_consumer_replaces_only_direct_sdl_image_and_inherits_dynamic_requirements(self):
        class Base:
            requires = ['zlib/1','sdl_image/[^3.4.4]','sdl_mixer/3','xz_utils/5']
            requirements = mock.Mock()
        dependency = types.ModuleType('dependencies.conanfile')
        dependency.VCMI = Base
        module = imported(ROOT/'conanfile.py','synthetic_root_consumer',{'dependencies.conanfile':dependency})
        self.assertEqual(module.VCMIApp.requires,
            ['zlib/1','sdl_image/3.4.4@newhorizons/patched','sdl_mixer/3','xz_utils/5'])
        self.assertEqual(Base.requires[1],'sdl_image/[^3.4.4]')
        self.assertIs(module.VCMIApp.requirements,Base.requirements)
        self.assertIn('29751304a13d25ac513f24305fa25b06a6edd9607718c90129b8350d35fc5573',
                      (RECIPE/'conandata.yml').read_text())


if __name__ == '__main__':
    unittest.main()
