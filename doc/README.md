# Documentation build (`doc/` and `convert.sh`)

This folder is the **output directory** for the BurrTools user guide. The sources live in [`../doc_src/`](../doc_src/), not here.

`convert.sh` is the original build script. It was meant to be run from this directory (`cd doc && ./convert.sh`) and to leave PDFs and split HTML in `doc/` for publishing (historically [the SourceForge user guide](https://burrtools.sourceforge.net/gui-doc/toc.html)).

It is **not** wired into Meson or GitHub Actions. Nothing in the current build runs it.

## What the script does

1. Copies screenshots from `doc_src/*.png` into `doc/`.
2. Reads a version string from `configure.ac` and prepends `User Guide for BurrTools <version>` to `doc_src/userGuide.t2t`.
3. Converts that txt2tags file to LaTeX via the vendored [`doc_src/txt2tags.py`](../doc_src/txt2tags.py), then runs `pdflatex` four times to produce A4 and US Letter PDFs.
4. Downscales the large window screenshots with ImageMagick `mogrify` and converts the same source to HTML, then splits it with `htmldoc`.

Related but separate: [`doxygen.cfg.in`](../doxygen.cfg.in) generates **library** API docs from comments in `src/lib` and `src/tools` into a `gendoc/` directory. That is also leftover from the Autotools era and is not run by Meson or CI. The published copy used to live at [the SourceForge lib docs](https://burrtools.sourceforge.net/lib-doc/index.html).

## What is broken

`convert.sh` cannot run as-is on this tree.

- **Missing `configure.ac`.** The version line is parsed from a file that was deleted when the project switched to Meson. Version now comes from `meson.build` (`git describe`, falling back to `0.7.0-unknown`).
- **Not part of the build.** Meson and `.github/workflows/` never invoke this script, so the repo does not produce user-guide PDFs or HTML.
- **Generated files land in the source tree.** PNGs, `.tex`, `.html`, and PDFs are written into `doc/` next to the script instead of a build directory.
- **Host-tool assumptions.** Needs GNU `sed -i` (breaks on macOS BSD sed), ImageMagick `mogrify`, `pdflatex` (TeX Live + KOMA-Script `scrbook`), and `htmldoc`. None of these are documented as project dependencies.
- **Stale encoding.** `userGuide.t2t` is declared `latin1`. A modern pipeline should be UTF-8.
- **Doxygen is similarly stale.** `doxygen.cfg.in` still uses Autotools `@VERSION@` substitution. Meson does not generate `doxygen.cfg` from it.

The in-app GUI help window that used to embed this HTML (`src/help/helpdata.cpp`) was removed from the tree; `meson.build` still lists those sources in places. That is a separate cleanup, not something `convert.sh` can fix.

## How to fix the current pipeline

Minimal repair if we want the existing txt2tags → PDF/HTML flow working again:

1. **Take the version from git or Meson**, not `configure.ac`. For example:

   ```sh
   title=$(git describe --tags --always 2>/dev/null || echo unknown)
   ```

   Or read `meson.project_version()` from a Meson custom target so the guide version matches the binary.

2. **Write outputs to a build directory** (`build/doc/` or `doc/out/`) and gitignore it. Keep `doc/` for scripts and this README only.

3. **Replace GNU `sed -i`** with a portable rewrite of the paper-size line (or generate two TeX files).

4. **List the host tools** in `BUILD.md` (or a short section here): TeX Live, ImageMagick, htmldoc.

5. **Optionally add a Meson target or a GitHub Actions job** that runs the script so a PDF can be attached to Releases. That is enough to restore the old user guide; it does not improve how docs are *read* on GitHub.

Fixing `doxygen.cfg.in` is similar: drop `@VERSION@`, point `INPUT` at `src/lib` and `src/tools`, and run Doxygen from CI if we still want API HTML.

These repairs keep a 2000s toolchain (txt2tags, pdflatex, htmldoc). That is fine as a stopgap. For docs that show up on the GitHub repo itself, see below.

## Future improvements

The useful split is:

| Audience | What they need | Current source |
| --- | --- | --- |
| Users | How to use the GUI, puzzles, solver, export | `doc_src/userGuide.t2t` |
| Contributors | How the code is laid out, how to build | `README.md`, `BUILD.md` |
| Library / algorithms | How voxels, assemblers, disassembly, etc. work | Doxygen comments in `src/lib` |

GitHub already renders Markdown in the repo. The root [`README.md`](../README.md) is the landing page. Anything we add as `.md` files and link from there is visible without extra tooling.

### GitHub Wiki vs files in the repo

The **Wiki** tab on the GitHub repo page is **not** built from files in this repository. It is a separate git repo (`<this-repo>.wiki.git`). Pages are edited on github.com or by cloning that extra repo. They do not version with code, do not show up in pull requests, and will drift from the user guide in `doc_src/`.

Do not put documentation in the Wiki if the goal is “docs live with the code and appear when you open the repo.” Wiki is a reasonable extra scratchpad; it is a poor source of truth.

Prefer one of these, in order:

1. **Markdown in the repo, linked from the root README** (simplest). Convert or rewrite the user guide as `.md` under `doc/` (or a new `docs/`). GitHub renders each file when you click it. Screenshots stay in `doc_src/` or move next to the Markdown. No CI required for people to read it.

2. **GitHub Pages** if we want a real website (searchable HTML, a table of contents, published Doxygen). GitHub can serve a `docs/` folder on the default branch, or a workflow can deploy HTML. That *is* generated from the main repo. Note the folder name: Pages’ built-in option is `docs/` (plural); this directory is `doc/` (singular). Either rename, or publish with Actions from wherever the HTML is built.

3. **Wiki only as a mirror**, if we still want the Wiki tab filled. A workflow can copy Markdown from this repo into the wiki git remote. The wiki would not be the place people edit.

### Regenerating “how the software works”

- **Convert `userGuide.t2t` to Markdown.** txt2tags can emit other formats; a one-time conversion (even partly manual) is better than keeping latin1 postproc macros, `htmldoc`, and pdflatex in the critical path. Keep the long user guide in git as Markdown so GitHub displays it.
- **Publish Doxygen from CI.** The library comments are already the best description of algorithms. A workflow can run Doxygen and push HTML to GitHub Pages (for example `/lib-doc/`). Refresh `doxygen.cfg.in` so it no longer depends on Autotools. Optionally add a short `doc/architecture.md` that points at the important classes (`puzzle_c`, `voxel_c`, assemblers, disassemblers) instead of duplicating Doxygen.
- **Root README as the index.** Keep build instructions in `BUILD.md`. Add links there to the user guide Markdown, architecture notes, and (if Pages is on) the generated API docs. Replace the SourceForge-only links once we host our own copies.
- **Do not compile the user guide back into the binary** unless the in-app help window is restored on purpose. External docs that track the repo are easier to update than `helpdata.cpp`.

A reasonable end state: open the GitHub repo → README points at `doc/` Markdown for users and a Pages URL for Doxygen. `convert.sh` can remain as a PDF generator for Releases, or be dropped once Markdown + Pages cover the same content.
