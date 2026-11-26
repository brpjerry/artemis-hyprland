# Arch Linux package

`PKGBUILD` builds the `artemis-hyprland` pacman package from this source tree.
It provides `artemis-qt` and conflicts with both `artemis-qt` and
`artemis-qt-git`.

## CI and releases

- **CI:** `.github/workflows/arch-package.yml` builds the package on pushes
  to `main` and on pull requests, and uploads it as a workflow artifact. It
  can also be run by hand on any branch from the Actions tab. CI versions look
  like `0.6.7.r1234.gabcdef12`; CI builds never publish a release.
- **Releases:** push a tag `v<version>` (e.g. `v0.6.8`). `release-arch.yml`
  builds the package with that version and attaches it to the tag's GitHub
  release, together with `SHA256SUMS` and a pacman repository database
  (`artemis-hyprland.db` / `.files`). To rebuild an existing tag (e.g. after
  an Arch library soname bump), run the workflow by hand with the tag and a
  `pkgrel` higher than any already published; it refuses anything lower. A
  rebuild replaces the tag's database, so leave immutable releases off in the
  repository settings.
- Bump `app/version.txt` before tagging; the app reports that version.

The releases can be used directly as a pacman repository (packages are unsigned):

```ini
[artemis-hyprland]
SigLevel = Optional
Server = https://github.com/brpjerry/artemis-hyprland/releases/latest/download
```

## Building locally

The same script CI uses, in a disposable container. Run it from the root of a
standalone, full Git clone (not a linked worktree or a shallow clone): the
script uses the clone's `.git` directory to initialize submodules and count
commits.

```sh
mkdir -p packaging/arch/out
podman run --rm -v "$PWD:/src:ro" -v "$PWD/packaging/arch/out:/out" \
    docker.io/library/archlinux:base-devel /src/packaging/arch/build-in-container.sh /out
```

Set `-e ARTEMIS_PKGVER=0.6.8 -e ARTEMIS_PKGREL=1` to build a release version.
The script copies the tree and initializes the submodules it needs itself.

Or directly on an Arch system, with the submodules initialized:

```sh
cd packaging/arch && makepkg -si
```

`makepkg` updates `pkgver=` in the PKGBUILD as it builds; don't commit that.
