#!/usr/bin/env python3
"""Run real Lua spell formulas; callback doubles do not prove native bindings/state."""
import ctypes
import ctypes.util
from pathlib import Path
import unittest


class PrimaryScaleLuaTest(unittest.TestCase):
    def execute(self, name, modern):
        library = ctypes.util.find_library(name)
        if not library:
            self.skipTest(name + ' is not installed')
        lua = ctypes.CDLL(library)
        lua.luaL_newstate.restype = ctypes.c_void_p
        lua.luaL_openlibs.argtypes = [ctypes.c_void_p]
        lua.lua_close.argtypes = [ctypes.c_void_p]
        lua.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
        lua.lua_tolstring.restype = ctypes.c_char_p
        state = lua.luaL_newstate()
        self.assertTrue(state)
        try:
            lua.luaL_openlibs(state)
            fixture = str(Path(__file__).parent / 'fixtures/primary-scale.lua').encode()
            if modern:
                lua.luaL_loadfilex.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
                lua.lua_pcallk.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ssize_t, ctypes.c_void_p]
                result = lua.luaL_loadfilex(state, fixture, None)
                if result == 0:
                    result = lua.lua_pcallk(state, 0, 0, 0, 0, None)
            else:
                lua.luaL_loadfile.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
                lua.lua_pcall.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
                result = lua.luaL_loadfile(state, fixture)
                if result == 0:
                    result = lua.lua_pcall(state, 0, 0, 0)
            error = lua.lua_tolstring(state, -1, None) if result else b''
            self.assertEqual(result, 0, (error or b'unknown Lua error').decode(errors='replace'))
        finally:
            lua.lua_close(state)

    def test_luajit(self):
        self.execute('luajit-5.1', False)

    def test_lua54(self):
        self.execute('lua5.4', True)


if __name__ == '__main__':
    unittest.main()
