# 场景资源与初始化预设

本次预设是可复现的场景初始化入口，不是场景保存系统。默认启动
`shadow-playground`；原来的两个锈铁球与 TwoPrimitives 场景保留为 `original`。

## 使用方式

构建后，在 **Scene Editor → Scene → Scene Presets** 选择预设，点击
**Load / Reset Scene**。下拉选择本身不改变当前场景；按钮会在下一帧开始前
替换对象，重置相机、灯光、材质参数和选择状态。当前编辑不会保存。

也可在启动时选择：

```bash
cmake --build --preset linux-debug --parallel
./build/linux-debug/vulkan --list-scenes
./build/linux-debug/vulkan --scene material-lab
./build/linux-debug/vulkan --scene shadow-playground
./build/linux-debug/vulkan --scene metal-rough-spheres
./build/linux-debug/vulkan --scene damaged-helmet
./build/linux-debug/vulkan --scene box
./build/linux-debug/vulkan --scene original
```

`--frames N` 在实际提交 N 帧后正常退出，供启动、Validation 与退出回归使用，
不代表性能基准。例如：

```bash
./build/linux-debug/vulkan --scene metal-rough-spheres --frames 30
```

## 场景用途

| ID | 内容 | 当前用途 |
|---|---|---|
| `material-lab` | 两排共十个球体和地面，金属度 0/1、粗糙度 0.05/0.25/0.5/0.75/1 | 纯色 PBR、IBL、高光与材质编辑 |
| `shadow-playground` | 大地面、近中远箱子、五根距离标记柱、门框、斜面、球体 | 单方向光阴影、PCF、bias 和后续 CSM；初始关闭 IBL 与 ambient |
| `original` | 两个锈铁球及 TwoPrimitives | 保留原演示作为回归入口 |
| `metal-rough-spheres` | Khronos 纹理版 MetalRoughSpheres | 官方材质参考、多 primitive、材质共享与取景 |
| `damaged-helmet` | 现有 DamagedHelmet GLB | 多纹理槽的实际资产 |
| `box` | 现有官方 Box GLB | 最小导入、取景和切换回归 |
| `sponza` | 完整建筑资产已下载，暂不可运行 | 待 alpha-mask / 双面材质在主 pass 和 shadow pass 中正确支持 |
| `metal-rough-spheres-no-textures` | 官方无纹理版，暂不可运行 | 上游材质全部双面；保留原语义，不修改标志绕过校验 |

所有可运行预设都用现有渲染路径，不隐含 CSM、延迟渲染或光追支持。
Skybox 仍然是背景；阴影预设的 `IBL intensity = 0` 只关闭环境光照贡献。

## 相机与阴影范围

- 程序使用 Z-up；glTF 导入层已转换 Y-up，不对资产再做一次旋转或缩放。
- glTF 展示预设通过每个物体局部 AABB 的八角变换得到世界包围范围，自动设置
  相机位置、yaw/pitch、裁剪距离和移动速度。内置实验场景采用固定观察点。
- 当前仍是一张方向光 shadow map。加载预设时将其投影范围拟合到场景包围球，
  避免较大场景超出原来固定的原点 ±5 范围。
- 范围只在加载/重置预设时计算。之后把对象移动出范围不会自动更新它。
  阴影场景远处可能出现低分辨率边缘，这是单张 shadow map 的限制，不是 CSM。

## 切换安全与资源所有权

- UI 仅提交切换请求；实际加载发生在 `beginFrame()` 之前。
- 等待所有在途帧结束后，暂存旧对象并构建新场景。解析、上传或分配失败时，
  释放本次新增资源，恢复旧场景并在 UI 显示错误。
- 成功后归还旧场景材质的 descriptor sets，再释放不再引用的网格、图像与 sampler。
  三个启动基础材质及环境图、IBL 资源保留；基础材质的 CPU 参数恢复初值。
- 普通材质容量仍为 128。descriptor pool 额外保留 16 个仅场景替换事务可用的
  slots，避免手动导入占满后无法重置。当前所有可运行预设新增材质均不超过 16；
  新增大型预设时必须同步重新设计这个预算，不能直接移除检查。
- 场景资源同步上传，切换大型资产时会短暂停顿；没有实现后台流式加载。

## 来源、许可与资源恢复

见 [资产清单](../assets/models/gltf/README.md) 和
[固定版本下载清单](../assets/models/gltf/scene-assets.json)。本次下载固定到
Khronos `9429648735279342b4c32b8745f7904196607379`，没有替换现有 Helmet GLB。
不支持的资产也完整保留，不静默忽略其 primitives 或修改材质。

```bash
git lfs pull
python3 scripts/fetch-scene-assets.py --verify-only
# 如有缺失文件，可下载缺失项；已有不同内容的文件不会被覆盖。
python3 scripts/fetch-scene-assets.py
```

新增图片和两个大型 bin 按 `.gitattributes` 使用 LFS，小型 accessor fixtures
继续使用普通 Git。下载脚本不 stage、commit 或 push。

Sponza 的上游许可证是 CRYENGINE Limited License；Helmet 的上游记录包含
CC-BY 和 CC-BY-NC 来源。公开发布或商业使用前请核对对应资产条款并保留署名，
不要把所有测试资源统一标为 CC0。

资源在可执行文件构建后复制到 `build/<preset>/assets`。若构建完成后才单独下载
资源，需要重新同步资源目录（只读源目录、不删除目标）：

```bash
cmake -E copy_directory assets build/linux-debug/assets
```

## 验收要点

- 多次循环加载所有六个可运行预设：同一预设的对象数和材质 set 数应保持稳定。
- 在 Inspector 改材质和变换后重置，相机、灯光与材质参数应恢复初值。
- 暂不支持的预设显示原因且禁用加载；CLI 请求也应清楚报错。
- 新资产损坏/缺少 bin 或纹理时，切换失败但旧场景仍可绘制。
- 切换后检查 resize、最小化恢复与正常退出的 Validation 输出。

## 本次验证记录（2026-09-23）

- Linux Debug / Release 构建通过；两个配置的 CTest 均为 2/2 通过。
- 下载清单的 91 个文件通过离线大小与 Git blob SHA-1 校验。
- 在 Xvfb 中运行 Debug 程序，依次切换六个可运行预设并检查画面；重复重置
  Material Lab、MetalRoughSpheres 和 Shadow Playground，对象数与材质 set 数稳定。
- 临时移走构建目录中的 MetalRoughSpheres bin 后尝试切换：明确报告
  `MissingExternalBuffer`，原场景继续绘制；恢复 bin 后可以重新加载。
- 检查 Sponza 禁用提示、1920×1080 → 1280×720 → 1920×1080 窗口缩放和
  正常关闭，运行期间及退出时没有 Validation Error。
- 尚未覆盖真实桌面窗口管理器的最小化/恢复、跨平台运行及长期显存压力测试。
  本次验证也不代表阴影质量已收敛：大范围单张 shadow map 的分辨率与 bias
  仍需通过阴影预设观察、调节。
