"""Exercise cross-repository version reservations using local Git remotes only."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SDK = Path(__file__).resolve().parents[3]
HOST = SDK.parent.parent
SPEC = importlib.util.spec_from_file_location('sdk_reservation', SDK / 'ci/scripts/reserve_build_number.py')
RESERVATION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESERVATION)


class SdkBuildNumberTest(unittest.TestCase):
    def git(self, root, *args):
        return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE, text=True).strip()

    def seed(self, path, remote):
        path.mkdir()
        self.git(path, 'init', '-b', 'feature/test')
        self.git(path, 'config', 'user.name', 'SDK migration test')
        self.git(path, 'config', 'user.email', 'sdk-test@example.invalid')
        self.git(path, 'init', '--bare', str(remote))
        self.git(path, 'remote', 'add', 'origin', str(remote))

    def setup_repositories(self, directory):
        root = Path(directory)
        sdk = root / 'sdk-seed'
        self.seed(sdk, root / 'sdk.git')
        (sdk / 'version').mkdir()
        (sdk / 'version/version.ini').write_text('version=0.1.0\nbuild=12\nstage=Debug\n')
        self.git(sdk, 'add', '.')
        self.git(sdk, 'commit', '-m', 'SDK source baseline')
        self.git(sdk, 'push', 'origin', 'HEAD:feature/test')
        self.git(root / 'sdk.git', 'symbolic-ref', 'HEAD', 'refs/heads/feature/test')
        host = root / 'host-seed'
        self.seed(host, root / 'host.git')
        helper = host / 'build_and_pack/scripts/jenkins/reserve-build-number'
        helper.parent.mkdir(parents=True)
        shutil.copy2(HOST / 'build_and_pack/scripts/jenkins/reserve-build-number', helper)
        self.git(host, 'submodule', 'add', str(root / 'sdk.git'), 'src/sdk_open')
        self.git(host, 'config', '-f', '.gitmodules', 'submodule.src/sdk_open.branch', 'feature/test')
        self.git(host, 'add', '.')
        self.git(host, 'commit', '-m', 'Host source baseline')
        self.git(host, 'push', 'origin', 'HEAD:feature/test')
        self.git(host / 'src/sdk_open', 'config', 'user.name', 'SDK migration test')
        self.git(host / 'src/sdk_open', 'config', 'user.email', 'sdk-test@example.invalid')
        return host

    @patch.dict(os.environ, {'GIT_ALLOW_PROTOCOL': 'file', 'GIT_CONFIG_NOSYSTEM': '1'})
    def test_reservation_publishes_sdk_before_host_pointer(self):
        with tempfile.TemporaryDirectory(prefix='sdk-reservation-') as directory:
            host = self.setup_repositories(directory)
            result = RESERVATION.reserve(host, 'feature/test')
            sdk = host / 'src/sdk_open'
            self.assertEqual(result['PRODUCT_BUILD'], '13')
            self.assertEqual(self.git(host, 'rev-parse', 'HEAD'), result['BUILD_COMMIT'])
            self.assertEqual(self.git(host, 'rev-parse', 'HEAD:src/sdk_open'), result['SDK_OPEN_COMMIT'])
            self.assertEqual(self.git(sdk, 'rev-parse', 'origin/feature/test'), result['SDK_OPEN_COMMIT'])
            self.assertEqual(self.git(sdk, 'diff-tree', '--no-commit-id', '--name-only', '-r', 'HEAD'), 'version/version.ini')
            self.assertEqual(self.git(host, 'diff-tree', '--no-commit-id', '--name-only', '-r', 'HEAD'), 'src/sdk_open')
            self.assertEqual(self.git(host, 'status', '--porcelain'), '')
            again = RESERVATION.reserve(host, 'feature/test')
            self.assertEqual(again['PRODUCT_BUILD'], '14')

    @patch.dict(os.environ, {'GIT_ALLOW_PROTOCOL': 'file', 'GIT_CONFIG_NOSYSTEM': '1'})
    def test_unknown_sdk_branch_fails_before_publishing(self):
        with tempfile.TemporaryDirectory(prefix='sdk-reservation-') as directory:
            host = self.setup_repositories(directory)
            before = self.git(host, 'rev-parse', 'HEAD')
            with self.assertRaisesRegex(Exception, 'No SDK branch'):
                RESERVATION.reserve(host, 'feature/test', 'missing-branch')
            self.assertEqual(self.git(host, 'rev-parse', 'HEAD'), before)
            self.assertEqual(self.git(host / 'src/sdk_open', 'show', 'HEAD:version/version.ini').split('build=')[1].splitlines()[0], '12')


if __name__ == '__main__':
    unittest.main()
