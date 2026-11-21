# Verified source for Echo 2 release 0.16.8

The adjacent [Git bundle](tested-source.bundle) contains the public community
source changes above LibreEcho-UI upstream base `b14f9eb4dc8343769eca86240bc258c9130fcc6b`.
The 0.16.8 source commit is `984ebc22cf68f1900e931d1129357db91951ea06`, with Git
tree `41b650f94bd108acb2c57e6c073712cf70ce8dd6`. The tree matches the UI
source used for the installed build. The public 0.16.3 source is the parent.
Commit identities use the fork's public account.

In a clone containing the upstream base, run these commands from the project root.

```sh
git bundle verify docs/community/history/tested-source.bundle
git fetch docs/community/history/tested-source.bundle HEAD
git rev-parse FETCH_HEAD
git rev-parse 'FETCH_HEAD^{tree}'
```

The two hashes should match the commit and tree above. See the
[product release notes](https://github.com/sn45k58myn-dev/LibreEcho/blob/docs/community-publication/docs/community/0.16.8.md) for the other source repositories and
first-install inputs.
