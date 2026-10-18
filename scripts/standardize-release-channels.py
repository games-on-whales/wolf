#!/usr/bin/env python3
"""Reconcile the organization's branches using existing administrator credentials.

Dry-run by default. Snapshot every repository before applying any changes. Never
rewrite tags, delete branches, disable protections, or leave archives unarchived.
"""
import argparse
import json
import os
import time
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request

ORG = 'games-on-whales'

class GitHub:
    def __init__(self, token):
        self.token = token

    def request(self, method, path, body=None):
        req = urllib.request.Request('https://api.github.com' + path,
            data=json.dumps(body).encode() if body is not None else None,
            headers={'Authorization': 'Bearer ' + self.token,
                     'Accept': 'application/vnd.github+json',
                     'X-GitHub-Api-Version': '2022-11-28'}, method=method)
        with urllib.request.urlopen(req, timeout=60) as response:
            raw = response.read()
            return json.loads(raw) if raw else None

    def repositories(self):
        result = []
        page = 1
        while True:
            batch = self.request('GET', f'/orgs/{ORG}/repos?per_page=100&page={page}')
            result.extend(batch)
            if len(batch) < 100:
                return result
            page += 1


def optional(api, path):
    try:
        return api.request('GET', path)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            error.close()
            return None
        raise


def snapshot(api, repo):
    root = '/repos/' + repo['full_name']
    detail = api.request('GET', root)
    default = detail['default_branch']
    branch = api.request('GET', root + '/branches/' + urllib.parse.quote(default, safe=''))
    latest = optional(api, root + '/releases/latest')
    released_sha = None
    if latest:
        if latest['draft'] or latest['prerelease']:
            raise ValueError('GitHub latest release must not be a draft or prerelease')
        commit = api.request('GET', root + '/commits/' + urllib.parse.quote(latest['tag_name'], safe=''))
        released_sha = commit['sha']
    stable = optional(api, root + '/git/ref/heads/stable')
    beta = optional(api, root + '/git/ref/heads/beta')
    return {'repository': detail['full_name'], 'default': default,
            'default_sha': branch['commit']['sha'], 'archived': detail['archived'],
            'admin': detail.get('permissions', {}).get('admin', False),
            'beta_sha': beta['object']['sha'] if beta else None,
            'stable_sha': stable['object']['sha'] if stable else None,
            'latest_release': latest['tag_name'] if latest else None,
            'release_sha': released_sha}


def restore_alias(api, root, name, sha):
    # Rename is asynchronous. The old name can remain temporarily unavailable.
    ref = '/git/ref/heads/' + urllib.parse.quote(name, safe='')
    for attempt in range(10):
        current = optional(api, root + ref)
        if current:
            if current['object']['sha'] != sha:
                raise RuntimeError('Legacy alias changed concurrently; refusing to overwrite it')
            return
        try:
            api.request('POST', root + '/git/refs', {'ref': 'refs/heads/' + name, 'sha': sha})
            return
        except urllib.error.HTTPError as error:
            if error.code not in (409, 422) or attempt == 9:
                raise
            time.sleep(3)


def reconcile(api, plan):
    root = '/repos/' + plan['repository']
    default = plan['default']
    unarchived = False
    try:
        if plan['archived']:
            api.request('PATCH', root, {'archived': False})
            unarchived = True
        if not plan['beta_sha']:
            if default not in ('beta', 'stable'):
                api.request('POST', root + '/branches/' + urllib.parse.quote(default, safe='') + '/rename', {'new_name': 'beta'})
                # Keep old public raw/download URLs working during the rollout.
                restore_alias(api, root, default, plan['default_sha'])
            else:
                api.request('POST', root + '/git/refs', {'ref': 'refs/heads/beta', 'sha': plan['default_sha']})
        if default != 'beta':
            api.request('PATCH', root, {'default_branch': 'beta'})
        stable_sha = plan['release_sha'] or plan['stable_sha'] or plan['beta_sha'] or plan['default_sha']
        if not plan['stable_sha']:
            api.request('POST', root + '/git/refs', {'ref': 'refs/heads/stable', 'sha': stable_sha})
        elif stable_sha != plan['stable_sha']:
            api.request('PATCH', root + '/git/refs/heads/stable', {'sha': stable_sha, 'force': True})
        # Read back both refs and the default; HTTP success alone is not enough.
        after = api.request('GET', root)
        beta = api.request('GET', root + '/git/ref/heads/beta')
        stable = api.request('GET', root + '/git/ref/heads/stable')
        if after['default_branch'] != 'beta' or not beta['object']['sha'] or stable['object']['sha'] != stable_sha:
            raise RuntimeError('Branch verification failed')
        return {'repository': plan['repository'], 'status': 'migrated', 'stable_sha': stable_sha,
                'release': plan['latest_release'], 'baseline_only': plan['release_sha'] is None}
    finally:
        if unarchived:
            api.request('PATCH', root, {'archived': True})
            if not api.request('GET', root)['archived']:
                raise RuntimeError('Failed to restore archived state')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--report', default='release-channel-migration.json')
    args = parser.parse_args()
    token = os.environ.get('GH_TOKEN')
    if not token:
        parser.error('GH_TOKEN must contain an existing organization administrator token')
    api = GitHub(token)
    plans = [snapshot(api, repo) for repo in api.repositories()]
    for plan in plans:
        print(json.dumps({'plan': plan}), flush=True)
    report = {'plans': plans, 'results': []}
    target = Path(args.report)
    target.write_text(json.dumps(report, indent=2) + '\n')
    missing = [p['repository'] for p in plans if not p['admin']]
    if missing:
        raise SystemExit('No changes made: missing repository administration access: ' + ', '.join(missing))
    if not args.apply:
        print(f'Validated {len(plans)} repositories; dry-run report: {target}')
        return
    for plan in plans:
        try:
            result = reconcile(api, plan)
        except Exception as error:
            result = {'repository': plan['repository'], 'status': 'blocked', 'error': str(error)}
        report['results'].append(result)
        target.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(result), flush=True)
    if any(r['status'] != 'migrated' for r in report['results']):
        raise SystemExit('Some repositories could not be migrated; see the report. Protections were not changed.')

if __name__ == '__main__':
    main()
