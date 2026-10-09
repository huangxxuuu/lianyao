<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

炼药应用使用 [`fonts/potion_font_16.c`](fonts/potion_font_16.c)：这是从 Adobe
Source Han Sans SC Regular 提交
`a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2` 生成的 16 px、4 bpp、未压缩
LVGL 字体子集。源 OTF 的 SHA-256 为
`f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256`。子集包含
可打印 ASCII 以及 [`fonts/potion_ui_zh_CN.txt`](fonts/potion_ui_zh_CN.txt)
列出的字符，使用 `lv_font_conv` 1.5.3 和参数 `--size 16 --bpp 4 --format
lvgl --no-compress --lv-font-name potion_font_16 --lv-include lvgl.h` 生成。
源字体采用 SIL Open Font License 1.1；随附许可见
[`fonts/SourceHanSansSC-LICENSE.txt`](fonts/SourceHanSansSC-LICENSE.txt)。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

炼药应用把生成的源图集保存在 `potion_art/source/`，把裁切后的运行时预览保存在
`potion_art/sprites/`。固定的内置图像生成提示词记录在
[`potion_art/generation-prompts.txt`](potion_art/generation-prompts.txt)。材料和正式药剂图集
生成于 2026-10-06；大锅、浑浊药剂、属性、导航和雷达状态图集生成于 2026-10-07。
这些素材由内置图像生成工具为本项目原创生成，不含第三方标记或文字。

在仓库根目录运行 `python tools/generate_potion_assets.py`，可以按固定网格使用最近邻缩放
进行裁切、量化透明边缘、写出 `main/potion_assets.c` 中的 RGB565A8 描述符，并重新生成
[`potion_art/sprite-preview.png`](potion_art/sprite-preview.png)。生成后的运行时尺寸分别为：
材料 32 × 32、正式药剂和导航 40 × 40、大锅 96 × 80、浑浊药剂 48 × 48、属性 24 × 24、
雷达状态 20 × 20。

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
