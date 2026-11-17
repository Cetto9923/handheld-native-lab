"""Exercise production patch loading against a self-owned synthetic NSO."""
import ctypes
import errno
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import platform
import struct

ROOT = Path(__file__).resolve().parents[2]


class Result(ctypes.Structure):
    _fields_ = [("status", ctypes.c_int), ("native_error", ctypes.c_int),
                ("cleanup_error", ctypes.c_int)]


class StartupCheats(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = ROOT / ".local" / "tests"
        output.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="startup-cheats-", dir=output)
        cls.output = Path(cls.temporary.name)
        library = cls.output / "loader.dylib"
        inputs = [str(ROOT / "src/runtime/nso/nso_loader.c"),
                  str(ROOT / "src/runtime/hle/hid_bridge.c")]
        if platform.machine().lower() in ("arm64", "aarch64"):
            inputs.append(str(ROOT / "tests/native/r3_trampoline_contract.S"))
        subprocess.run(["clang", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-DNSO_LOADER_TESTING", *inputs,
                        "-o", str(library)], check=True)
        cls.loader = ctypes.CDLL(str(library))
        cls.loader.nso_load_file.argtypes = [ctypes.c_char_p, ctypes.c_void_p,
                                             ctypes.c_char_p, ctypes.c_size_t]
        cls.loader.nso_load_file.restype = ctypes.c_void_p
        cls.loader.nso_base.argtypes = [ctypes.c_void_p]
        cls.loader.nso_base.restype = ctypes.c_uint64
        cls.loader.nso_apply_startup_patch_file.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                                          ctypes.c_void_p]
        cls.loader.nso_apply_startup_patch_file.restype = Result
        cls.loader.nso_entry_address.argtypes = [ctypes.c_void_p, ctypes.c_uint64,
                                                ctypes.POINTER(ctypes.c_uint64)]
        cls.loader.nso_entry_address.restype = Result
        cls.loader.nso_require_executor.argtypes = [ctypes.c_void_p]
        cls.loader.nso_free.argtypes = [ctypes.c_void_p]
        cls.loader.nso_loader_test_vm.argtypes = [ctypes.c_void_p] * 3
        cls.loader.ffrs_hid_r3_address.restype = ctypes.c_void_p
        cls.loader.ffrs_hid_publish.argtypes = [ctypes.c_int, ctypes.c_uint64,
                                                ctypes.c_int32, ctypes.c_int32,
                                                ctypes.c_int32, ctypes.c_int32]
        spec = importlib.util.spec_from_file_location("loader_fixture", ROOT / "tests/native/test_loader.py")
        fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        cls.image = cls.output / "fixture.nso"
        fixture.fixture(cls.image)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        error = ctypes.create_string_buffer(256)
        self.module = self.loader.nso_load_file(bytes(self.image), None, error, len(error))
        self.assertTrue(self.module, error.value)
        self.base = self.loader.nso_base(self.module)

    def tearDown(self):
        self.loader.nso_loader_test_vm(None, None, None)
        self.loader.nso_free(self.module)

    def apply(self, rows, build_id="0" * 32, size="100", r3=None):
        plan = self.output / "patch.plan"
        plan.write_text("NSO-TEXT-PATCHES-1\n" + build_id + "\n" + size + "\n" + rows)
        return self.loader.nso_apply_startup_patch_file(self.module, bytes(plan), r3)

    def word(self, offset):
        return ctypes.c_uint32.from_address(self.base + offset).value

    def test_disabled_plan_and_missing_file_are_noops(self):
        self.assertEqual(self.apply("").status, 0)
        missing = self.output / "missing.plan"
        self.assertEqual(self.loader.nso_apply_startup_patch_file(self.module, bytes(missing), None).status, 0)
        self.assertEqual(self.word(0x80), 0)

    def test_text_words_and_expected_guards(self):
        self.assertEqual(self.apply("80 0 d503201f\n84 0 0\nc0 0 d503201f\n").status, 0)
        self.assertEqual(self.word(0x80), 0xd503201f)
        self.assertEqual(self.word(0xc0), 0xd503201f)
        self.assertEqual(self.word(0x84), 0)

    def test_wrong_build_and_text_size(self):
        self.assertNotEqual(self.apply("80 0 d503201f\n", build_id="1" * 32).status, 0)
        self.assertNotEqual(self.apply("80 0 d503201f\n", size="101").status, 0)
        self.assertEqual(self.word(0x80), 0)

    def test_late_mismatch_does_not_apply_earlier_word(self):
        self.assertNotEqual(self.apply("80 0 d503201f\n84 1 2\n").status, 0)
        self.assertEqual(self.word(0x80), 0)

    def test_duplicates_unsorted_unaligned_and_nontext_are_rejected(self):
        for rows in ["80 0 1\n80 0 2\n", "84 0 1\n80 0 2\n",
                     "81 0 1\n", "4000 30444f4d 0\n", "fffffffffffffffc 0 1\n"]:
            with self.subTest(rows=rows):
                self.assertNotEqual(self.apply(rows).status, 0)
                self.assertEqual(self.word(0x80), 0)

    def test_malformed_and_oversized_values_do_not_partially_apply(self):
        for suffix in ["garbage\n", "84 0 100000000\n", "84 -1 0\n", "84 0 0 extra\n"]:
            with self.subTest(suffix=suffix):
                self.assertNotEqual(self.apply("80 0 1\n" + suffix).status, 0)
                self.assertEqual(self.word(0x80), 0)

    def test_record_limit(self):
        rows = "".join(f"{0x100 + i * 4:x} 0 1\n" for i in range(513))
        self.assertNotEqual(self.apply(rows).status, 0)
        self.assertEqual(self.word(0x100), 0)

    def test_published_module_rejects_patching(self):
        self.loader.nso_require_executor(self.module)
        self.assertNotEqual(self.apply("80 0 1\n").status, 0)
        self.assertEqual(self.word(0x80), 0)

    def fail_protection_call(self, failure):
        system = ctypes.CDLL(None, use_errno=True)
        system.mprotect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        calls = [0]
        callback_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t,
                                         ctypes.c_int, use_errno=True)

        def protect(address, size, permissions):
            calls[0] += 1
            if calls[0] == failure:
                ctypes.set_errno(errno.EACCES)
                return -1
            return system.mprotect(address, size, permissions)

        callback = callback_type(protect)
        self.loader.nso_loader_test_vm(None, callback, None)
        result = self.apply("80 0 d503201f\n")
        self.loader.nso_loader_test_vm(None, None, None)
        return result

    def test_write_permission_failure_preserves_text(self):
        self.assertNotEqual(self.fail_protection_call(1).status, 0)
        self.assertEqual(self.word(0x80), 0)

    def test_execute_permission_failure_invalidates_module(self):
        self.assertNotEqual(self.fail_protection_call(2).status, 0)
        address = ctypes.c_uint64()
        self.assertNotEqual(self.loader.nso_entry_address(self.module, 0x80, ctypes.byref(address)).status, 0)

    def load_r3_fixture(self, text_size=0x100):
        self.loader.nso_free(self.module)
        self.module = None
        data = bytearray(self.image.read_bytes())
        if text_size > 0x100:
            data.extend(bytes(max(0, 0x200 + text_size - len(data))))
        struct.pack_into("<I", data, 0x18, text_size)
        struct.pack_into("<II", data, 0x280, 0x1e2a4d20, 0xd65f03c0)
        path = self.output / "r3.nso"
        path.write_bytes(data)
        error = ctypes.create_string_buffer(256)
        self.module = self.loader.nso_load_file(bytes(path), None, error, len(error))
        self.assertTrue(self.module, error.value)
        self.base = self.loader.nso_base(self.module)

    def test_r3_plan_rejects_missing_input_and_late_mismatch_atomically(self):
        self.load_r3_fixture()
        self.assertNotEqual(self.apply("R3 80 1e2a4d20 1e2703e0\n").status, 0)
        address = self.loader.ffrs_hid_r3_address()
        for rows in ["R3 80 1e2a4d20 1e2703e0\n84 1 0\n",
                     "R3 80 1e2a4d20 d503201f\n"]:
            self.assertNotEqual(self.apply(rows, r3=address).status, 0)
            self.assertEqual(self.word(0x80), 0x1e2a4d20)

    def test_r3_plan_rejects_insufficient_padding(self):
        self.load_r3_fixture(text_size=0x3ff0)
        self.assertNotEqual(self.apply("R3 80 1e2a4d20 1e2703e0\n", size="3ff0",
                                       r3=self.loader.ffrs_hid_r3_address()).status, 0)
        self.assertEqual(self.word(0x80), 0x1e2a4d20)

    @unittest.skipUnless(platform.machine().lower() in ("arm64", "aarch64"),
                         "requires native AArch64 execution")
    def test_r3_trampoline_executes_preserving_registers_flags_and_stack(self):
        self.load_r3_fixture()
        address = self.loader.ffrs_hid_r3_address()
        self.assertEqual(self.apply("R3 80 1e2a4d20 1e2703e0\n", r3=address).status, 0)
        invoke = self.loader.ffrs_r3_invoke
        invoke.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p,
                           ctypes.c_float, ctypes.c_float]
        invoke.restype = None
        for connected, buttons, expected_held in [(1, 0, False), (1, 32, True),
                                                  (1, 16, False), (0, 32, False),
                                                  (1, 32, True), (1, 0, False)]:
            self.loader.ffrs_hid_publish(connected, buttons, 0, 0, 0, 0)
            for condition in range(16):
                output = (ctypes.c_uint64 * 7)()
                flags = condition << 28
                invoke(self.base + 0x80, flags, output, 3.0, 5.0)
                value = ctypes.cast(output, ctypes.POINTER(ctypes.c_float))[0]
                normal = 3.0 if flags & 0x80000000 else 5.0
                self.assertEqual(value, normal if expected_held else 0.0)
                self.assertEqual(output[1], flags)
                self.assertEqual(output[1], output[2], "NZCV changed")
                self.assertEqual(output[3], output[4], "SP changed")
                self.assertEqual(output[5], 0x1234, "x16 changed")
                self.assertEqual(output[6], 0x5678, "x17 changed")


if __name__ == "__main__":
    unittest.main()
