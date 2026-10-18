import importlib.util
from pathlib import Path
import unittest
import urllib.error

path = Path(__file__).resolve().parents[1] / 'standardize-release-channels.py'
spec = importlib.util.spec_from_file_location('channels', path)
channels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(channels)

class API:
    def __init__(self, default='main', beta=None, stable=None, archived=False, reject_stable=False):
        self.default = default
        self.archived = archived
        self.refs = {default: 'development'}
        if beta: self.refs['beta'] = beta
        if stable: self.refs['stable'] = stable
        self.writes = []
        self.reject_stable = reject_stable

    def request(self, method, path, body=None):
        endpoint = path.removeprefix('/repos/games-on-whales/example')
        if method != 'GET': self.writes.append((method, endpoint, body))
        if not endpoint:
            if body:
                self.default = body.get('default_branch', self.default)
                self.archived = body.get('archived', self.archived)
            return {'default_branch': self.default, 'archived': self.archived}
        if endpoint.endswith('/rename'):
            old = endpoint.split('/')[2]
            self.refs[body['new_name']] = self.refs.pop(old)
            if self.default == old: self.default = body['new_name']
        elif method == 'POST' and endpoint == '/git/refs':
            name = body['ref'].removeprefix('refs/heads/')
            if name in self.refs: raise RuntimeError('ref already exists')
            self.refs[name] = body['sha']
        elif endpoint.startswith('/git/ref/heads/'):
            name = endpoint.removeprefix('/git/ref/heads/')
            if name not in self.refs: raise urllib.error.HTTPError(path, 404, 'missing', {}, None)
            return {'object': {'sha': self.refs[name]}}
        elif endpoint == '/git/refs/heads/stable':
            if self.reject_stable: raise RuntimeError('protected branch')
            self.refs['stable'] = body['sha']
        else:
            raise AssertionError((method, endpoint))
        return {}

    def plan(self, release='released'):
        return {'repository': 'games-on-whales/example', 'default': self.default,
                'default_sha': self.refs[self.default], 'archived': self.archived,
                'beta_sha': self.refs.get('beta'), 'stable_sha': self.refs.get('stable'),
                'release_sha': release, 'latest_release': 'v2026.10' if release else None}

class ReleaseChannels(unittest.TestCase):
    def test_rename_preserves_legacy_download_refs_and_released_stable(self):
        for old in ['main', 'master', 'develop']:
            with self.subTest(old=old):
                api = API(default=old)
                result = channels.reconcile(api, api.plan())
                self.assertEqual(api.default, 'beta')
                self.assertEqual(api.refs, {'beta': 'development', old: 'development', 'stable': 'released'})
                self.assertEqual(result['status'], 'migrated')

    def test_existing_beta_is_never_overwritten(self):
        api = API(beta='newer-development', stable='old-release')
        channels.reconcile(api, api.plan())
        self.assertEqual(api.refs['beta'], 'newer-development')
        self.assertEqual(api.refs['main'], 'development')
        self.assertEqual(api.refs['stable'], 'released')

    def test_default_stable_is_preserved_as_beta_before_release_pointer_moves(self):
        api = API(default='stable')
        channels.reconcile(api, api.plan())
        self.assertEqual(api.refs, {'beta': 'development', 'stable': 'released'})
        self.assertEqual(api.default, 'beta')

    def test_unreleased_repo_gets_baseline_without_inventing_release(self):
        api = API()
        result = channels.reconcile(api, api.plan(release=None))
        self.assertTrue(result['baseline_only'])
        self.assertIsNone(result['release'])
        self.assertEqual(api.refs['stable'], 'development')

    def test_archive_is_restored_even_when_protection_refuses_update(self):
        api = API(archived=True, stable='protected', reject_stable=True)
        with self.assertRaisesRegex(RuntimeError, 'protected branch'):
            channels.reconcile(api, api.plan())
        self.assertTrue(api.archived)
        self.assertNotIn('protection', str(api.writes))

    def test_success_restores_archive_and_is_idempotent(self):
        api = API(archived=True)
        channels.reconcile(api, api.plan())
        self.assertTrue(api.archived)
        channels.reconcile(api, api.plan())
        self.assertTrue(api.archived)
        self.assertEqual(api.refs['stable'], 'released')

if __name__ == '__main__': unittest.main()
