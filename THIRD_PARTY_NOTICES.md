# Third-party notices

Moonbow is licensed under the GNU General Public License v3.0 or later ([LICENSE](LICENSE)). It includes or uses the
following works. Their licenses apply to those parts.

## Shipped with Moonbow

### libmpv (mpv) and the libraries it is built with

`libmpv-2.dll` in the installer and the zip is the libmpv build `mpv-dev-x86_64-20261017-git-dde0671072` from
[shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20261017). It contains
[mpv](https://mpv.io) and the libraries that build links in, including [FFmpeg](https://ffmpeg.org), libass,
libplacebo, FreeType, HarfBuzz and others.

- License: this build is distributed under the **GNU General Public License v3**, because mpv and FFmpeg are built
  with their GPL parts enabled.
- Corresponding source: mpv at commit
  [`dde0671072`](https://github.com/mpv-player/mpv/commit/dde0671072); the exact versions of every other library
  and the build scripts are in shinchiro/mpv-winbuild-cmake at the
  [`20261017`](https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20261017) release.

### Fonts

| Font | Files | License |
| --- | --- | --- |
| [Inter](https://github.com/rsms/inter), © The Inter Project Authors | `res/fonts/Inter-*.ttf` | SIL Open Font License 1.1, [res/fonts/Inter-LICENSE.txt](res/fonts/Inter-LICENSE.txt) |
| [Onest](https://github.com/googlefonts/onest), © The Onest Project Authors | `res/fonts/Onest-Variable.ttf` | SIL Open Font License 1.1, [res/fonts/Onest-LICENSE.txt](res/fonts/Onest-LICENSE.txt) |

### Code and shaders

- `res/shaders/moonbow-cas.glsl`: contrast-adaptive sharpening after
  [AMD FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS), MIT License.

### Interface design and icons

The layout of the windows, the type scale and the icon set (`design/icons`) started from the Figma Community file
**"VLC Redesign (Community)"** and were then changed: monochrome palette, new logo, Russian interface, new
screens. The reference exports in `design/ref` come from that file.

## Used by the website (`site/`)

| Work | License |
| --- | --- |
| [Astro](https://astro.build) (build only) | MIT |
| [GSAP](https://gsap.com) with ScrollTrigger, SplitText and CustomEase, bundled into `site.js` | [GSAP Standard License](https://gsap.com/standard-license) |
| [Onest](https://github.com/googlefonts/onest), [Geist Mono](https://github.com/vercel/geist-font) | SIL Open Font License 1.1 |
| Photographs on the screenshots (frames and covers of the made-up demo media), from [Unsplash](https://unsplash.com) via Lorem Picsum | [Unsplash License](https://unsplash.com/license); authors in [site/src/data/credits.json](site/src/data/credits.json) |

## Optional, not shipped

- [yt-dlp](https://github.com/yt-dlp/yt-dlp) (Unlicense) is used only if you put `yt-dlp.exe` next to Moonbow.
- Online services, only when you turn them on: [LRCLIB](https://lrclib.net), [OpenSubtitles](https://www.opensubtitles.com),
  [TMDB](https://www.themoviedb.org) (this product uses the TMDB API but is not endorsed or certified by TMDB).
