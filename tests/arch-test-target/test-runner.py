#!/usr/bin/env python3
"""Exercise ACT runner failure reporting without requiring an ISA toolchain."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parents[2] / "tools/run-arch-test-elfs.sh"


class ActRunnerTest(unittest.TestCase):
    def test_results_and_selection(self):
        with tempfile.TemporaryDirectory(prefix="act runner ") as tmp:
            root = Path(tmp)
            emu = root / "emulator"
            emu.write_text(
                '#!/bin/sh\ncase "$4" in\n'
                "*fail.elf) echo mismatch; exit 1;;\n"
                "*hang.elf) exec sleep 10;;\nesac\n"
            )
            emu.chmod(0o755)
            elfs = root / "elfs"
            ext = elfs / "rv32i/I"
            ext.mkdir(parents=True)

            def run(extensions="I"):
                return subprocess.run(
                    [str(RUNNER), str(emu), str(elfs), extensions],
                    env={**os.environ, "ACT_TIMEOUT": "0.1"},
                    capture_output=True,
                    text=True,
                    timeout=10,
                )

            self.assertEqual(run().returncode, 2)  # Empty extension
            (ext / "pass.elf").touch()
            result = run("I,I")  # Repeated selections run each ELF once
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("1/1 passed", result.stdout)
            (ext / "fail.elf").touch()
            result = run()
            self.assertEqual(result.returncode, 1)
            self.assertIn("mismatch", result.stdout)
            self.assertIn("1/2 failed", result.stderr)
            (ext / "hang.elf").touch()
            result = run()
            self.assertEqual(result.returncode, 1)
            self.assertIn("exit=124", result.stdout)
            self.assertIn("2/3 failed", result.stderr)
            self.assertEqual(run("M").returncode, 2)
            for category, extension in [("priv", "ZicsrF"), ("rv32e", "E")]:
                selected = elfs / category / extension
                selected.mkdir(parents=True)
                (selected / "pass.elf").touch()
                result = run(extension)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("1/1 passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
