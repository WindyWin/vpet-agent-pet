# Whale Girl artwork terms

The frames and preview in `assets/whale-girl/` are adapted from
[Multimodal-AI-Companion-Assets](https://github.com/oyxdsg/Multimodal-AI-Companion-Assets),
release [v1.0.0](https://github.com/oyxdsg/Multimodal-AI-Companion-Assets/releases/tag/v1.0.0).
The downloaded `deskpet-animation-assets-v1.0.0.zip` has SHA-256
`276c85901546682ba585dbe2cf8f1587599b607078e8d5c02e68b72de3f67979`.
The upstream license and notice below were retained from revision
`f43ffc4aa16013609fdbcf77116f017fe2b0d04a`.

Character: **「溟月」 © 上善无形**. Whale-maid design: **© ZipZipPipe** (Bilibili).
Animation frames made and processed by **Multimodal-AI-Companion**.
Upstream declares this artwork **CC BY-NC-SA 4.0**: attribution, non-commercial use and
share-alike. Agent Pet's adapted frames and preview are provided under those same terms.
The application code has a separate license. Upstream's claims about the original creators'
permission have not been independently verified by Agent Pet.

Agent Pet's `scripts/import_whale_girl.py` retains all 1,662 animation frames at their original
pixel size, aligns their visible feet on transparent 600 × 600 canvases, restores the jump/roll
motion from `traj.json`, and assigns the upstream 40 ms frame interval (25 fps). Sustained activity and sleep states hold a selected pose between the entry and exit portions
of the performance. The error cue omits 16 neutral lead-in frames to fit its gesture within the
app’s four-second error window; all source frames remain available in the animation preview.
The original static fallback is not used. The preview is downscaled from the first front frame.
The manifest records exact adapted frame sizes and SHA-256 hashes.

## Upstream LICENSE (verbatim)

# 许可（License）

**本仓库内容不适用 MIT。**

本仓库是 [Multimodal-AI-Companion](https://github.com/oyxdsg/Multimodal-AI-Companion)
的动画资源包。其中的**角色形象不是本项目原创**，而是他人作品的衍生：

| 环节 | 作者 | 时间 |
|---|---|---|
| 原创 OC「溟月」 | **上善无形** | 2025-06 |
| 女仆鲸鱼娘（现流行形象） | **ZipZipPipe**（B 站） | 2026-04-25 |

据两位作者 2026-08-02 的公开声明，该形象以
**Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International
（CC BY-NC-SA 4.0）** 开放二次创作。

## 这意味着

- **署名（BY）**：转载、分发、二次创作时**必须标注原作者**；
- **非商业（NC）**：**不得用于商业目的**；
- **相同方式共享（SA）**：衍生作品**必须**以 CC BY-NC-SA 4.0（或兼容协议）发布。

协议全文：<https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode.zh-Hans>
协议摘要：<https://creativecommons.org/licenses/by-nc-sa/4.0/deed.zh-Hans>

## 建议的署名格式

> 角色形象：「溟月」© 上善无形（CC BY-NC-SA 4.0）；
> 女仆鲸鱼娘版 © ZipZipPipe（CC BY-NC-SA 4.0）。
> 动画帧由 Multimodal-AI-Companion 项目制作与处理。

## 想用不受该协议约束的素材

用 [deskpet-tools](https://github.com/oyxdsg/Multimodal-AI-Companion/tree/main/deskpet-tools)
从**你自己的**素材制作角色包（`desktop-pet/skins/`），即可完全绕开上述约束。

> 若原作者对某个具体用途另有授权，以实际授权为准。

## Upstream NOTICE.md (verbatim)

# 素材声明（NOTICE）

## ⚠️ 角色形象：非本项目原创，不适用 MIT

「大肥鱼 / 女仆鲸鱼娘」有明确的上游创作链，**版权属于原作者**：

| 环节 | 作者 | 时间 | 说明 |
|---|---|---|---|
| 原创 OC「溟月」 | **上善无形** | 2025-06 | 蓝色渐变长发、鲸类头鳍、鲸尾等基础设定 |
| 女仆鲸鱼娘（现流行形象） | **ZipZipPipe**（B 站） | 2026-04-25 | 在「溟月」基础上融入 DeepSeek 元素、改深蓝白色女仆装 |

据两位作者 2026-08-02 的公开声明，该形象以 **CC BY-NC-SA 4.0** 开放二次创作：
**需署名、禁止商业使用、衍生作品须相同方式共享**。全文见 [`LICENSE`](LICENSE)。

## 本仓库内容

- **动画帧**（`<动作>/frame_*.png`，17 个动作 / 1663 帧）：由本项目制作与处理
  （绿幕抠图 + 去绿溢色 + 站姿统一 + 不动点对齐），但**画面描绘的是上述角色形象**，
  因此**属于该角色的衍生作品**，按 **CC BY-NC-SA 4.0** 使用，**不适用 MIT**。
- 处理管线说明见源码仓库 `desktop-pet/docs/视频处理成图片成为宠物动作.md`。

## 不含

- **绿幕源视频**（`素材源/`）：离线加工输入，不随本仓库发布。
- **车万女仆（Touhou Little Maid）皮肤资源**：属另一项目（SmartMaid），不在本仓库。
- **Minecraft Wiki 派生数据**：与素材无关，见源码仓库 `NOTICE`。
