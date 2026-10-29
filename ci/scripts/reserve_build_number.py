#!/usr/bin/env python3
"""Reserve SDK version in its repository, then publish the host gitlink.

Only CI calls this mutating command. A pinned SDK source commit is required;
branch selection never replaces it with an unrelated branch tip.
"""
import argparse
import importlib.machinery
import importlib.util
import os
from pathlib import Path
import shutil
import sys
import tempfile


def load_helper(host):
    path = host / 'build_and_pack/scripts/jenkins/reserve-build-number'
    loader = importlib.machinery.SourceFileLoader('sdk_build_number_core', str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    previous_bytecode = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous_bytecode
    return module


def reserve(host, branch_value, sdk_branch_value=None):
    host = host.resolve()
    core = load_helper(host)
    git = core.git
    branch = core.normalize_branch(branch_value)
    remote = core.fetch_branch(host, branch)
    core.ensure_workspace_can_fast_forward(host, remote)
    git(host, '-c', 'submodule.recurse=false', 'merge', '--ff-only', '--no-edit', remote)
    git(host, 'submodule', 'update', '--init', '--', 'src/sdk_open')
    sdk = host / 'src/sdk_open'
    pinned = git(sdk, 'rev-parse', 'HEAD').stdout.strip()
    # Prefer the matching feature/release branch, then the declared SDK branch.
    declared = git(host, 'config', '-f', '.gitmodules', '--get',
                   'submodule.src/sdk_open.branch', check=False).stdout.strip()
    candidates = [sdk_branch_value] if sdk_branch_value else [branch, declared]
    sdk_branch = None
    for candidate in candidates:
        if not candidate or candidate == '.':
            continue
        candidate = core.normalize_branch(candidate)
        probe = git(sdk, 'ls-remote', '--exit-code', '--heads', 'origin',
                    'refs/heads/' + candidate, check=False)
        if probe.returncode:
            continue
        tip = core.fetch_branch(sdk, candidate)
        if git(sdk, 'merge-base', '--is-ancestor', pinned, tip, check=False).returncode == 0:
            sdk_branch = candidate
            break
    if sdk_branch is None:
        raise core.ReservationError('No SDK branch descends from the pinned commit; set SDK_OPEN_BRANCH explicitly')
    previous = core.parse_ini_fields(core.read_commit_file(sdk, tip, 'version/version.ini'))['build']
    values = core.reserve(sdk, sdk_branch, 'version/version.ini')
    sdk_commit = values['BUILD_COMMIT']
    temporary = Path(tempfile.mkdtemp(prefix='reserve-sdk-gitlink-'))
    worktree = temporary / 'worktree'
    added = False
    try:
        git(host, 'worktree', 'add', '--detach', str(worktree), remote)
        added = True
        for attempt in range(core.MAX_PUSH_ATTEMPTS):
            base = core.fetch_branch(host, branch)
            for flags in ([], ['--cached']):
                if git(host, 'diff', *flags, '--quiet', '--ignore-submodules=all', check=False).returncode:
                    raise core.ReservationError('Host workspace contains tracked modifications')
            if git(host, 'merge-base', '--is-ancestor', 'HEAD', base, check=False).returncode:
                raise core.ReservationError('Host workspace cannot fast-forward to origin')
            git(worktree, 'reset', '--hard', base)
            entry = git(worktree, 'ls-tree', 'HEAD', '--', 'src/sdk_open').stdout.split()
            if len(entry) < 3 or entry[0] != '160000':
                raise core.ReservationError('src/sdk_open is not a submodule in the selected host branch')
            git(sdk, 'fetch', '--no-tags', 'origin', sdk_branch)
            if git(sdk, 'merge-base', '--is-ancestor', entry[2], sdk_commit, check=False).returncode:
                raise core.ReservationError('Host SDK pointer advanced beyond this reservation; retry without regressing it')
            git(worktree, 'update-index', '--add', '--cacheinfo', '160000,' + sdk_commit + ',src/sdk_open')
            if git(worktree, 'diff', '--cached', '--quiet', check=False).returncode == 0:
                main_commit = base
            else:
                subject = 'Chore(build-number): reserve sdk-open build {0} -> {1} [skip ci]'.format(previous, values['PRODUCT_BUILD'])
                git(worktree, 'commit', '-m', subject,
                    '-m', 'Build-Number-Version-File: src/sdk_open\nSDK-Open-Commit: ' + sdk_commit)
                main_commit = git(worktree, 'rev-parse', 'HEAD').stdout.strip()
                pushed = git(worktree, 'push', 'origin', 'HEAD:refs/heads/' + branch, check=False)
                if pushed.returncode:
                    latest = core.fetch_branch(host, branch)
                    if latest == base or git(host, 'merge-base', '--is-ancestor', base, latest, check=False).returncode:
                        raise core.ReservationError('Unable to publish SDK pointer: ' + pushed.stderr.strip())
                    continue
            git(host, '-c', 'submodule.recurse=false', 'merge', '--ff-only', '--no-edit', main_commit)
            values['SDK_OPEN_COMMIT'] = sdk_commit
            values['BUILD_COMMIT'] = main_commit
            return values
        raise core.ReservationError('Host branch repeatedly advanced; retry the Jenkins build')
    finally:
        if added:
            git(host, 'worktree', 'remove', '--force', str(worktree), check=False)
        shutil.rmtree(str(temporary), ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--branch', required=True)
    parser.add_argument('--sdk-branch', default=os.environ.get('SDK_OPEN_BRANCH'))
    args = parser.parse_args()
    try:
        result = reserve(args.repo, args.branch, args.sdk_branch)
    except Exception as error:
        print('reserve-sdk-build-number: ' + str(error), file=sys.stderr)
        return 1
    for key, value in result.items():
        print('{0}={1}'.format(key, value))
    return 0


if __name__ == '__main__':
    sys.exit(main())
