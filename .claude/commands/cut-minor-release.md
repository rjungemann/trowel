---
description: Cut a new minor release. Bump CMakeLists version, update CHANGELOG + README, tag, push.
argument-hint: (no arguments)
allowed-tools: Bash, Read, Edit, AskUserQuestion
---

# Cut a new minor release

Run a full minor-version release of Trowel. Order matters: the commit
must contain the bumped version + CHANGELOG + README before the tag is
created, and the tag push must include the bump commit so the GitHub
Actions release workflow builds against the right sources.

The source of truth for the version is `CMakeLists.txt` line 3:
`project(trowel VERSION X.Y.Z ...)`. There is no separate VERSION file.

## Preconditions (verify before doing anything destructive)

Run these in parallel and report findings before proceeding:

1. `git status --porcelain` -- working tree must be clean. If not, stop
   and ask the user to commit or stash.
2. `git rev-parse --abbrev-ref HEAD` -- must be `main`. If not, stop and
   ask the user to switch.
3. `git fetch origin main` followed by
   `git rev-list --left-right --count origin/main...HEAD` -- local main
   must not be behind origin. If behind, stop and ask the user to pull.
4. Read `CMakeLists.txt` and extract the current version from the
   `project(trowel VERSION X.Y.Z ...)` line.
5. `git describe --tags --abbrev=0 --match 'v[0-9]*'` -- the most recent
   non-test release tag (`v*` matches test tags too; filter to numeric).
   Should match `v<CURRENT>` unless this is the first real release. If
   there's a mismatch, surface it to the user before proceeding.
6. `git log <lastTag>..HEAD --oneline` (or full history if no prior
   release) -- there must be at least one commit since the last tag. If
   zero, refuse to release.

If any check fails, stop and report. Do not proceed without the user
explicitly overriding.

## Step 1: Compute the new version

Parse `MAJOR.MINOR.PATCH` from `CMakeLists.txt`. Compute
`NEW = MAJOR.(MINOR+1).0`.

Example: `0.1.4` -> `0.2.0`.

## Step 2: Draft the CHANGELOG entry

Run `git log <lastTag>..HEAD --pretty=format:'%h %s'` (or full log for
the first release) to get the commit list since the last tag.

Classify each commit into one of:
- **Added** -- new features, new views, new commands
- **Changed** -- behavior changes, renames, semantic shifts in existing features
- **Fixed** -- bug fixes (commits starting with `fix:`, "fix", or referencing a bug)
- **Removed** -- deletions of features or APIs
- **Docs** -- documentation-only changes (only include if non-trivial)
- **Internal** -- skip from changelog (CI, refactors with no user-visible effect, dependency bumps)

Skim each commit's subject line and, when ambiguous, run
`git show --stat <sha>` to see what files changed. Don't include every
internal commit -- the changelog audience is users, not the git log.

Format the new entry to match Keep a Changelog style:

```
## [NEW] -- YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Fixed
- ...
```

Use today's date (`date +%Y-%m-%d`). Omit empty subsections. Aim for
3-10 bullets total -- consolidate related commits into one bullet rather
than copying every subject line. Lead each bullet with a short bolded
title where appropriate.

## Step 3: Draft the README "Latest release" line

`README.md` has a line beginning with `**Latest release:**` near the top.
For the very first release, that line currently reads:

```
**Latest release:** _none yet -- the first tagged release will populate this line._
```

Replace it with:

```
**Latest release:** `v<NEW>` -- <one-sentence summary of the most significant change>.
```

On subsequent releases, replace the previous `**Latest release:**` line
with the same shape. Pick the one or two most significant items from the
changelog and write one sentence. Match a terse, action-oriented voice.

## Step 4: Confirm with the user

Show the user:
- The OLD -> NEW version transition
- The full CHANGELOG entry you drafted
- The new README "Latest release" line
- The list of commits that informed the changelog

Use `AskUserQuestion` with options:
- **Proceed**: continue with steps 5-8 as drafted
- **Edit the changelog**: ask the user what to change, then re-show
- **Edit the README line**: ask the user for the replacement sentence
- **Cancel**: stop without making any changes

Do not proceed past this step without explicit confirmation.

## Step 5: Apply file changes (no git operations yet)

In parallel:
1. Edit `CMakeLists.txt` -- change `VERSION <OLD>` to `VERSION <NEW>` on
   the `project(trowel ...)` line.
2. Edit `CHANGELOG.md` -- insert the new entry immediately after the
   `<!-- New releases are inserted immediately below this comment. -->`
   marker and before any existing `## [<OLD>]` entry. Keep one blank
   line between entries.
3. Edit `README.md` -- replace the `**Latest release:**` line.

After applying, run `git diff --stat` and show the user what changed.
Do not commit yet.

## Step 6: Commit and tag locally

```sh
git add CMakeLists.txt CHANGELOG.md README.md
git commit -m "$(cat <<'EOF'
chore: release v<NEW>

<one-paragraph summary copied from the README "Latest release" sentence
or the changelog's most significant items>

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
git tag -a "v<NEW>" -m "Release v<NEW>"
```

The tag exists locally only; nothing is pushed yet.

## Step 7: Push commit and tag

```sh
git push origin main
git push origin "v<NEW>"
```

The tag push triggers `.github/workflows/release.yml`, which builds the
macOS bundle, signs, notarizes, staples, zips, and publishes a GitHub
Release with `Trowel-<NEW>.zip` attached.

## Step 8: Verify

Wait briefly, then check:

```sh
gh run list --workflow=release.yml --limit 1
```

Report the run ID and status to the user. Do not block waiting for the
release workflow to finish -- macOS build + notarize takes 5-10 minutes
and the user can watch it themselves (`gh run watch <id>`).

End by reporting:
- The new version
- The commit SHA of the bump commit
- The Release workflow run URL
- A reminder that the release page will populate with `Trowel-<NEW>.zip`
  once the workflow finishes, and that the Cask's `sha256` needs to be
  updated from the release notes.

## Things to refuse

- Refuse to bypass any precondition without explicit user override.
- Refuse to skip the CHANGELOG/README updates -- they're load-bearing
  for users discovering the release.
- Refuse to use `git push --force` for any step here.
- Refuse to amend a commit that has already been pushed.
