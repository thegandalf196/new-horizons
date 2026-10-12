"""Legacy dependency consumer for notice-only repair of existing binaries.

Fresh player builds use the patched SDL_image requirement in root conanfile.py.
This consumer must not relabel an old binary as using the patched decoder.
"""
import importlib.util
from pathlib import Path
import sys

repository = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repository))
spec = importlib.util.spec_from_file_location('nh_root_conan_recipe', repository / 'conanfile.py')
root_recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(root_recipe)


class NoticeRepack(root_recipe.VCMIApp):
    requires = root_recipe.VCMI.requires
