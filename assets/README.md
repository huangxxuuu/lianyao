<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

The potion application uses [`fonts/potion_font_16.c`](fonts/potion_font_16.c),
a 16 px, 4 bpp, uncompressed LVGL subset generated from Adobe Source Han Sans
SC Regular at commit `a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2`. The source OTF SHA-256 is
`f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256`.
The subset contains printable ASCII plus the characters listed in
[`fonts/potion_ui_zh_CN.txt`](fonts/potion_ui_zh_CN.txt). It was generated with
`lv_font_conv` 1.5.3 using `--size 16 --bpp 4 --format lvgl --no-compress
--lv-font-name potion_font_16 --lv-include lvgl.h`. The source is licensed under
the SIL Open Font License 1.1; the redistributed license is
[`fonts/SourceHanSansSC-LICENSE.txt`](fonts/SourceHanSansSC-LICENSE.txt).

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

The potion application keeps its generated source atlases under
`potion_art/source/` and the cropped runtime previews under
`potion_art/sprites/`. The stable built-in image-generation prompt is recorded
in [`potion_art/generation-prompts.txt`](potion_art/generation-prompts.txt).
The material and formal-potion atlases were generated on 2026-10-06; the
brewing, muddy-potion, property, navigation, and radar-status atlases were
generated on 2026-10-07. They are original project assets created with the
built-in image generation tool and contain no third-party marks or text.

Run `python tools/generate_potion_assets.py` from the repository root to crop
the fixed grids with nearest-neighbor scaling, quantize transparent edges,
write the RGB565A8 descriptors in `main/potion_assets.c`, and rebuild
[`potion_art/sprite-preview.png`](potion_art/sprite-preview.png). Generated
runtime sizes are 32 × 32 for materials, 40 × 40 for formal potions and
navigation, 96 × 80 for cauldrons, 48 × 48 for muddy potions, 24 × 24 for
properties, and 20 × 20 for radar status.

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
