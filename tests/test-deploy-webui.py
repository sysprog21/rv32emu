#!/usr/bin/env python3
"""HTML-only deployment regression checks; no build toolchain required."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class HtmlDeploymentTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'tools').mkdir()
        shutil.copy(PROJECT_ROOT / 'tools/deploy-webui.sh', self.root / 'tools')
        shutil.copytree(PROJECT_ROOT / 'assets/wasm/html', self.root / 'assets/wasm/html')
        (self.root / '.config').write_text('CONFIG_ROOTFS_EXT4=y\n')
        # Baseline deploy, so failure tests have existing pages to protect.
        result = self.deploy()
        self.assertEqual(result.returncode, 0, result.stderr)

    def deploy(self):
        return subprocess.run(
            ['bash', str(self.root / 'tools/deploy-webui.sh'), 'html'],
            capture_output=True, text=True,
        )

    def test_rootfs_configuration(self):
        for config, rootfs, cli in [
            ('CONFIG_ROOTFS_EXT4=y\n', 'rootfs.ext4', '-x vblk:rootfs.ext4,rootfs'),
            ('# CONFIG_ROOTFS_EXT4 is not set\n', 'rootfs.cpio.gz', '-i rootfs.cpio.gz'),
        ]:
            with self.subTest(config=config):
                (self.root / '.config').write_text(config)
                result = self.deploy()
                self.assertEqual(result.returncode, 0, result.stderr)
                template = (self.root / 'assets/wasm/html/system.html').read_text()
                expected = template.replace('<ROOTFS>', rootfs).replace('<ROOTFS_CLI>', cli)
                self.assertEqual((self.root / 'demo/system/index.html').read_text(), expected)
                for source, target in [('user.html', 'user/index.html'), ('demo-index.html', 'index.html')]:
                    self.assertEqual(
                        (self.root / 'assets/wasm/html' / source).read_bytes(),
                        (self.root / 'demo' / target).read_bytes(),
                    )

    def snapshot(self):
        demo = self.root / 'demo'
        return {p: p.read_bytes() for p in demo.rglob('*') if p.is_file()}

    def assert_failure_keeps_output(self):
        before = self.snapshot()
        result = self.deploy()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('Deployment complete!', result.stdout)
        self.assertEqual(self.snapshot(), before)

    def test_missing_template_fails(self):
        for name in ['user.html', 'system.html', 'demo-index.html']:
            with self.subTest(template=name):
                path = self.root / 'assets/wasm/html' / name
                content = path.read_bytes()
                path.unlink()
                try:
                    self.assert_failure_keeps_output()
                finally:
                    path.write_bytes(content)

    def test_missing_config_fails(self):
        (self.root / '.config').unlink()
        self.assert_failure_keeps_output()


if __name__ == '__main__':
    unittest.main()
