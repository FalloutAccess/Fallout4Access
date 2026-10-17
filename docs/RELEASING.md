# Releasing

How a build becomes a download, and which parts a human still has to do.

## The shape of it

```
git tag v0.2.0  →  Build workflow  →  GitHub release (automatic)
                                          ↓
                              Publish to Nexus Mods (manual trigger)
```

Two stages on purpose. A GitHub release is cheap and deletable. A Nexus upload
emails everyone tracking the mod and its changelog endpoint is *additive* — post
the same version twice and the mod page shows the entry twice — so it never
happens without someone asking for it.

The repository lives at <https://github.com/FalloutAccess/Fallout4Access>
(`origin`), where the workflows run.

## Cutting a release

1. **Bump the version.** `xmake.lua`:

   ```lua
   local F4A_VERSION = "0.2.0"
   ```

   That is the only place it lives: the AE manifest and the OG
   `F4SEPlugin_Query` both read it. The build refuses to release when it
   disagrees with the tag, because F4SE's log is the first thing a bug report
   quotes and it must not claim a version the archive does not.

2. **Finish the CHANGELOG section.** `CHANGELOG.md` needs a `## v0.2.0 (…)`
   header. Its body becomes the GitHub release notes *and* the Nexus changelog,
   so it is the one place release text is written. The build refuses a tag
   without one.

3. **If any `.psc` changed, recompile and commit the `.pex` with it.**
   `xmake build` compiles them with the Creation Kit compiler; commit
   `Data/Scripts/F4A/*.pex` in the same commit as the source. See *Papyrus*
   below for why CI cannot do this, and run `pwsh tools/check-papyrus.ps1` to
   confirm before tagging.

4. **Commit, tag, push.**

   ```bash
   git commit -am "release 0.2.0"
   git tag v0.2.0
   git push origin main --tags
   ```

5. **Wait for the Build workflow.** On a tag it builds the plugin, packages the
   archive, and publishes a GitHub release carrying the zip and its SHA-256.

6. **Test the release zip in-game**, on both runtimes, installed through Mod
   Organizer 2. This is the real gate: nothing after it can be taken back.

7. **Publish to Nexus**: Actions → *Publish to Nexus Mods* → Run workflow, with
   the tag, or:

   ```bash
   gh workflow run publish-nexus.yml --repo FalloutAccess/Fallout4Access -f tag=v0.2.0
   ```

   It downloads the archive from the release, verifies the configuration, the
   version string and the archive layout, and only then uploads. A failed check
   stops it with nothing sent.

   The *dry run* box (`-f dry_run=true`) is optional and off by default. It runs
   the same checks and prints exactly what it would send — including the
   changelog after Markdown is converted to plain text — without sending it.
   Use it when the CHANGELOG section has tables, links or nested lists, or after
   changing the workflow: the changelog entry, the tracking email and archiving
   the previous version cannot be undone through the API.

8. **Check the Nexus Files tab by hand.** The API calling an upload "available"
   is not the same as it being downloadable — see below.

## One-time setup

Done on 2026-10-17; nothing to repeat unless the API key is rotated or the mod
page is recreated.

| Where | Name | Value | How to get it |
| --- | --- | --- | --- |
| **Org secret** (FalloutAccess) | `NEXUSMODS_API_KEY` | — | The account's *Personal API Key*, at the bottom of <https://www.nexusmods.com/settings/api-keys> |
| Repo **variable** | `NEXUSMODS_MOD_ID` | `4943507458010` | `(1151 << 32) \| 100314`; confirmed via the public v2 GraphQL API, `legacyMods.uid` |
| Repo **variable** | `NEXUSMODS_FILE_ID` | `3131410` | Run *Publish to Nexus Mods* (dry run) with it unset: the log lists the candidates; take the `f4a` entry |

The API key is one organisation secret shared by FalloutNVAccess and this repo
(visibility *selected*, so only repos granted it can read it). Rotating it is a
single change, and a new mod repo needs to be granted access rather than given
its own copy. Do **not** also set a repo secret of the same name: a repo secret
silently overrides the org one, so the two would drift apart on the next
rotation.

```bash
# Rotate the key (prompts for the value):
gh secret set NEXUSMODS_API_KEY --org FalloutAccess --visibility selected --repos FalloutNVAccess,Fallout4Access
# Which repos can see it:
gh api orgs/FalloutAccess/actions/secrets/NEXUSMODS_API_KEY/repositories --jq '.repositories[].full_name'
```

Managing org secrets needs the `admin:org` scope on the gh login
(`gh auth refresh -h github.com -s admin:org`).

Neither ID is a number you can read off the website. `NEXUSMODS_MOD_ID` is not
the 100314 from the page URL, and `NEXUSMODS_FILE_ID` is not the `fileId` the
website or the v2 API shows for a file (406083 for v0.1.0) — it is the v3
mod-file id, which only the authenticated v3 API returns
(`GET /v3/mods/4943507458010/files`, `data.mod_files`).

The key goes in a secret and the two IDs in variables — the IDs are not sensitive
and being able to read them in the run log is useful when something is wrong.

## What stays manual, permanently

- **The mod description, screenshots and page images.** Nexus has no endpoint
  to edit them.
- **The `Update` and `Old version` file categories.** The API accepts only
  `main`, `optional` and `miscellaneous`. Anything else is website-only.

## Papyrus: why the `.pex` files are committed

The three scripts under `Data/Scripts/Source/User/F4A` compile with the Creation
Kit's `PapyrusCompiler.exe`, which `xmake build` runs from the game folder. CI
has no game folder, and the compiler cannot be redistributed. The alternatives
were Caprica (open source, and what several Skyrim plugins use in CI) or a
private copy of the CK compiler (legally grey). Caprica would mean releasing
`.pex` files from a different compiler than the ones tested in-game, at exactly
the boundary the plugin calls into by name — so the compiled files are committed
instead, as F4MCM, PapyrusExtenderSSE and MCM Helper do.

`tools/check-papyrus.ps1` runs first in CI and inside the packaging script. It
fails when:

- a `.psc` declares a function or event its `.pex` string table does not
  contain — a function added or renamed without recompiling;
- a `.pex` was committed before its `.psc` (or, for uncommitted files, is older
  by mtime);
- a `.psc` has no tracked `.pex`, or a tracked `.pex` has no source.

It cannot prove a `.pex` was compiled from the *current* body of a function
whose name did not change. Recompiling and committing both together is still
the rule.

## Things that will bite

**Virus scanning is not instant, and this mod is a prime false positive.** The
upload API reporting `available` only means Nexus accepted the bytes. The file is
quarantined until scanning finishes. The mod ships an unsigned DLL that hooks a
running game process and patches memory. On the Files tab: green tick = passed
internal scanning and VirusTotal; blue = passed internally, VirusTotal
unavailable; yellow `?` = still scanning and nobody can download it; red cross =
failed and needs a moderator ticket. Always look before announcing a release.

**`display_name` names the file, not the mod.** The mod is *Fallout 4 Access*;
every file entry on its Files tab has been named `f4a`. The workflow passes
`f4a` — sending the mod title would rename the entry, and Nexus generates the
download filename from it (`{display_name}_{version}_{slug}.zip`). If you ever
rename the file on the website, change the workflow to match. Never put the
version in it, or you get `f4a 0.2.0_0.2.0_x7k2p.zip`.

**The changelog endpoint appends.** Re-running a publish for a version already up
adds a second changelog entry rather than replacing the first. If a publish half
fails, check the mod page before retrying.

**Version strings are validated.** `^[a-zA-Z0-9.-]+$`, 50 characters max. `+` is
rejected, so semver build metadata (`0.2.0+ci`) fails — after the archive has
already uploaded. The publish workflow checks this before it starts.

**The upload endpoints are still officially beta.** `Nexus-Mods/upload-action` is
pinned to the commit behind `v1.0.0-beta.10`, the same as FalloutNVAccess; do not
downgrade it (pre-beta.8 releases call the deprecated `createUpdateGroupVersion`)
and do not copy input names from tutorials written before June 2026. If the
publish breaks with no change on our side, check
<https://api.nexusmods.com/openapi.yaml> and the action's releases first.

## The archive

Rooted at the game's `Data` folder — `F4SE\`, `Scripts\`, `MCM\`, `Interface\`,
`Sound\` and `fallout4access.esp` at the top level, no wrapper directory — which
is what Mod Organizer 2 and Vortex expect. Releases up to v0.1.0 were packed by
hand inside a `fallout4access\` folder.

`Data\` content comes from `git ls-files`, never the working directory. Packing
the working tree is how earlier archives shipped ~10 MB of unreferenced sound
drafts and `.bak` files; the ESP references only the four `UI_Target*.wav` cues.
A sound that is not committed does not ship.

Prism is MPL-2.0 and statically linked, so its `LICENSE`, `NOTICE` and
`LICENSES\` tree ship in the archive; packaging fails without them.

## The CI toolchain

- `runs-on: windows-2025`, which carries Visual Studio 2026, matching the
  developer's `vs = "2026"`. xmake builds Prism and the plugin with the same
  toolchain, so there is no generator/toolset pair to keep in step as there is
  in FalloutNVAccess.
- xmake **3.1.1**, the version that wrote `xmake-requires.lock`.
- CMake **4.4.4** over the image's 3.31: Prism requires the C23 dialect, which
  CMake only enables for MSVC from 4.0.
- The xmake package cache is keyed on the runner image version and Prism's
  pinned commit, with no restore-keys. Prism is built with LTCG, so a `.lib`
  from a different MSVC fails to link (C1047) rather than degrading, and xmake
  itself does not notice a Prism submodule bump.

## Building the archive locally

```bash
xmake build
pwsh tools/package-release.ps1
```

Output lands in `dist/`. This is the same script CI runs, so what you get locally
is what ships. The version defaults to `F4A_VERSION` in `xmake.lua`.
