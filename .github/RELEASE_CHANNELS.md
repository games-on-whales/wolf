# Release channels

This repository follows the Games on Whales release convention:

- `beta`: the default development and integration branch.
- `stable`: the commit of the latest published, non-prerelease GitHub release.
- `vYYYY.MM`: immutable calendar release tags, for example `v2026.10`.

Feature branches and historical tags remain valid. Legacy `main`, `master`, or
`develop` aliases are retained during migration for existing download URLs.
For repositories with no release yet, `stable` is an initial snapshot, not a
claim that a release has been published. The first release replaces that baseline.

Create releases from reviewed commits and use the next unused calendar tag.
Do not move an existing release tag to publish a second build in the same month.
Publishing or editing a release synchronizes `stable` with GitHub's latest release;
prereleases never become stable. The release-channel workflow can also be run
manually to reconcile the branch after a release is selected as latest.
