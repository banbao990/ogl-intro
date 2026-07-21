# 08-SSR 技术路线

> 状态：固定步进、逐像素 DDA 与 Hi-Z 均已实现。当前默认使用 Pixel DDA，可在运行时切换 Off / Fixed Step / Pixel DDA / Hi-Z；DDA 的同格交叉与共享边/角点判定作为唯一 mip0 oracle，Hi-Z 负责保守跳空并在叶节点直接复用这些判定，不再重复遍历整条射线。

## 已确认范围

- SSR 仅作用于水面，不改造成面向全部材质的全屏通用 SSR。
- 场景保留一个普通物体，并继续使用现有前向 PBR 材质渲染。
- 不为 SSR 新增完整 G-buffer；水面的视空间位置和法线直接由水面顶点/片元着色器提供。
- 必须完成两个可用版本：
  1. 固定步进 ray marching，并使用命中区间二分细化；
  2. 基于深度金字塔的 Hi-Z ray marching。
- 在上述两个版本之外增加严格逐像素的 screen-space DDA，作为当前画面正确性基线与 Hi-Z 不适用方向的 fallback；固定步进继续保留用于教学和差异对照。
- 实施顺序固定为：先完成固定步进版本作为正确性基线，再实现 Hi-Z 版本并与基线对照验证。
- 所有算法同时保留，并通过 ImGui 在运行时提供 `Off / Fixed Step / Pixel DDA / Hi-Z` 四档切换。
- 最终水面采用完整合成：屏幕空间折射、水深吸收、SSR/环境反射、直接光高光与 Fresnel；同时保留 `SSR Only` 调试视图。
- 水面必须保持动态：两层波纹纹理按世界空间 XZ 坐标采样并随时间滚动；ImGui 暴露 `Wave Global Speed`、`Wave Scale 1/2`、`Wave Speed 1/2` 和波纹强度。默认两层速度非零，启动后即可直接观察运动。
- 最终颜色采用能量分配式混合：`refraction * (1 - F) + reflection * F`，不使用会无约束增亮的加法合成。
- 当前阶段 SSR miss、屏幕边缘和低置信度区域统一回退到现有 `_env_color * _env_strength`；不新增 cubemap/天空盒资源系统，但保留未来替换接口。
- Windows 配置和编译统一使用 `.agents/AGENTS.md` 规定的 `bat/` 工具链。
- 相机使用公共 `FPSCamera`：`W/S/A/D` 前后左右、`Q/E` 升降、左键拖动旋转、`Shift + 左键` 平移、滚轮调整视场角；与 ImGui 交互时屏蔽相机输入。

## 总体渲染结构

1. 将 PBR 物体渲染到 HDR 场景颜色和场景深度。
2. 在绘制水面前保留只读的场景颜色与场景深度，避免水面一边采样一边写入同一 attachment。
3. 水面片元根据视空间位置、扰动法线和观察方向产生反射射线。
4. 由当前选定的固定步进、逐像素 DDA 或 Hi-Z 路径寻找命中 UV，并从只读 HDR 场景颜色采样反射。
5. 水面只采样 snapshot，并把合成结果写回主 HDR 目标；随后沿用现有 ACES tone mapping 输出到屏幕，因此不存在 framebuffer feedback loop。

## 阶段 0：恢复可靠基线

- 恢复当前被无条件 `return` 禁用的 PBR 场景绘制。
- 修复深度 shader 路径和 `P * V * M` 矩阵顺序。
- 删除“把水面再次写入场景深度副本”的流程，避免 SSR 自相交。
- 为渲染目标使用明确的 sized format、`GL_CLAMP_TO_EDGE` 和合适的非 mip 过滤方式。
- 建立无 framebuffer feedback loop 的场景颜色、场景深度与水面合成资源。

## 阶段 1：固定步进验证版

- 在 view space 中从水面位置沿反射方向推进。
- 每步投影至屏幕 UV，越界即 miss。
- 将 OpenGL `[0, 1]` depth 转成 `[-1, 1]` NDC depth，通过逆投影矩阵重建 view-space depth。
- 使用可调 view-space thickness 判断光线是否穿过场景表面。
- 保存穿越前后的区间，命中后执行 4～6 次二分细化。
- 输出明确的 `hitValid`、`hitUV`、步数和 confidence，miss 不复用无效坐标。
- 加入屏幕边缘、最大距离与迭代预算衰减。

### Fixed Step 审计与修复

- 旧实现只在 `depthDelta` 从负到正时触发候选，朝相机方向的正到负交叉会整段漏失；现改为与 Pixel DDA 一致的双向符号 bracket，并只在两个端点都具有有效场景深度时进入细化。
- 旧实现即使没有合法 bracket，也会把上一步强行当作“前侧”执行二分；现要求端点同时满足符号交叉与深度连续性，二分过程中若遇到背景、离屏或不连续表面则拒绝该候选，避免跨 silhouette/nearest-depth 阶梯制造伪命中。
- 旧实现的第一个候选只要被 thickness 或二分拒绝，就立即终止整条射线；现只记录 `MISS_DEPTH_REJECTED` 事件并继续前进，后续合法交点仍可命中。只有整条射线最终没有命中时，调试视图才汇总显示该拒绝原因。
- 旧实现只测试 `N * Step Size <= Max Distance` 的整步，最后不足一步的尾段永远丢失，且 `Step Size > Max Distance` 时完全不采样；现先按 near/far 与 viewport 裁剪射线，再用 `min(nextStep, clippedEnd)` 显式测试最后一段。
- 当恰好用完最后一个允许步骤并到达射线终点时，现在优先返回 `MISS_DISTANCE`/`MISS_SCREEN`，不再误报 `MISS_ITERATIONS`；只有预算确实短于剩余射程时才显示黄色迭代耗尽。
- 每个粗步旧实现先投影一次，又在深度求值中重复投影一次；现合并为单次 Fixed Step sample 求值，并删除未使用的 `previousUV` 状态。
- `Iteration Count` 在 Fixed Step 下改用 `min(Max Steps, ceil(Max Distance / Step Size))` 归一化；默认 200 个有效步不再因为统一除以 4096 而几乎全黑。UI 同时显示 `Max Steps * Step Size` 限制后的 nominal 有效射程，并在参数组合无法覆盖 `Max Distance` 时给出提示；Fixed Step 的距离 confidence 也按该有效射程衰减，避免预算边界仍保持较高权重后突然截断。
- 上述修复不改变 Fixed Step 的方法定位：常量 view-space 步长在投影后仍可能一次跨过多个像素或完整跨过薄物体的“射线位于表面后方”区间。二分只能细化已经发现的合法 bracket，不能恢复两个采样点之间从未被观察到的交点；固定 view-Z thickness 也仍具有视角和深度依赖。因此 Fixed Step 是教学/差异对照路径，Pixel DDA 才是当前 mip0 覆盖基线。
- Fixed Step 不使用极大的单侧 depth slab 填补空洞，因为这会把已经深入 silhouette 后方的射线误判为命中并造成拉伸。jitter 也只能把规则条纹打散成噪点，不能恢复未采样的交点，因此不作为正确性修复。

## 阶段 1.5：逐像素 DDA 与连续表面修复

- 先在 view space 中按 `Max Distance`、near plane 和 far plane 裁剪反射射线，再投影起点与终点。
- 保存 `P/Q/k`：`P` 为像素坐标，`Q = viewPosition / clip.w`，`k = 1 / clip.w`；沿屏幕直线插值后通过 `Q/k` 恢复透视正确的视空间位置。
- 对投影线段做 viewport slab 裁剪；远端位于屏幕外时只裁到屏幕边界，不因终点离屏而丢弃屏内的有效射线段。
- 对投影线执行二维像素格 DDA，每一步从当前 texel 的入口精确推进到下一条 X/Y cell boundary；同一 cell 的闭区间出口根直接保留，避免下一个背景 texel 抹掉合法边界命中。
- 每一步通过 `texelFetch` 读取当前 mip0 texel 的场景深度，并使用该 cell 内 segment 两端的 `Q/k` view depth 建立双向符号 bracket。ray depth 无论远离还是接近相机，只要相对 scene depth 在 cell 内发生换号都可命中；这修复了旧实现只接受负到正穿越而导致部分相机角度整片缺失的问题。
- 同 cell 候选解析求解 `rayDepth == sceneDepth` 的位置，并返回该位置的屏幕 UV；只使用随场景深度缩放的微小数值 epsilon，不使用会跨越 silhouette 的巨大单侧 depth slab。
- 为修复离散 depth 阶梯跳过零点造成的规则空白条纹，DDA 保存上一有效 cell。相邻 texel 的边界误差发生任一方向换号且两份 depth 通过连续性检查时，直接把共享 cell boundary 作为离散阶梯交点；删除旧版把 texel center 正交投影到 ray 后再二分的错误参数化。
- 连续性阈值先把两份 view position 移到相同平均深度平面，得到一个屏幕像素对应的 view-space footprint `F`，再使用 `min(16F, 5% * meanDepth) + 4 * epsilon`。该阈值允许连续斜面具有合理深度梯度，同时仍拒绝背景和明显 silhouette 跳变。
- 精确角点同时跨越 X/Y 时执行 supercover：额外读取两个 side texel，只有存在一条由“上一格 → side → 当前格”组成、两条共享边均连续且其中包含双向 bracket 的路径时才接受角点命中；背景会断开路径。
- UI 中 DDA 明示 `1 pixel cell per step`、双向 crossing 与 continuity bridge，并隐藏仅 Fixed Step 使用的 `Step Size`、`Binary Steps` 和 `Thickness`；`Max Distance`、`Origin Bias`、`Edge Fade` 仍共用。
- 当前 shader/UI 最大预算为 `4096` 个访问步骤，默认也提高为 `4096`。二维 cell DDA 的最坏预算接近 `abs(dx) + abs(dy)`；这覆盖当前约 2048×1152 窗口的最坏 3200 格路径，预算不足仍明确输出黄色 `MISS_ITERATIONS`。
- 为严格基线将默认 `Thickness` 从 `0.15` 收紧为 `0.03`，`Origin Bias` 从 `0.03` 收紧为 `0.005`；Thickness 现在仅影响 Fixed Step，较小 Origin Bias 则避免三条路径跳过几乎贴住水面的头盔底部薄结构。Pixel DDA 与 Hi-Z 的最终 DDA 交叉不使用 `Thickness`。

## 阶段 2：Hi-Z 版本

- 创建独立 `GL_R32F` 深度金字塔；当前 OpenGL 使用 forward-Z，因此每层保存 2×2 texel 的最小深度。
- 动态计算 mip 数，正确处理窗口缩放和奇数分辨率。
- 使用 compute shader 逐层构建，层间设置必要的 image/texture memory barrier。
- 实现按屏幕 cell 边界推进的层次遍历：空区域升到粗 mip，潜在交点下降到细 mip。forward-Z min depth 只能证明一段射线仍位于该 cell 全部几何之前，因此 Hi-Z 本身不再产生命中。
- 首次到达 mip0 potential 后，只对当前 leaf 的闭区间复用 Pixel DDA 的 `Q/k` 同格交叉判定，并在 leaf 入口复用相同的共享边 continuity bridge 与角点 supercover。即使某个 coarse cell 可安全跳过，也会先解析其入口对应的叶级跨轴关系并检查这一个边界，避免遗漏“上一格在 ray 前方、当前格在 ray 后方”的离散阶梯交叉；内部 leaf 因 min depth 已证明全部位于 ray 后方，无需逐格访问。假候选越过当前 leaf 后继续 Hi-Z，不从射线起点重新执行完整 DDA。旧版在 mip0 cell 出口采到相邻 texel、用跨多格的 `lastFront` 建 bracket 并在不连续 depth 上二分，会把 silhouette 误认成命中并产生长拖影；该独立命中器已经删除。
- Hi-Z 层级遍历耗尽预算时明确返回黄色 `MISS_ITERATIONS`，不再追加一次完整 DDA。默认 4096 预算远高于正常层级遍历需要；较小的人工预算若不足，会如实显示而不会产生隐藏的高额 fallback 开销。
- Hi-Z 与 DDA/固定步进版本共用射线生成、深度约定、命中结果、颜色合成和 debug 输出；Hi-Z 叶节点直接调用 Pixel DDA 的命中辅助函数，因此保持同一套 mip0 正确性规则而不付出全路径 DDA 成本。
- forward-Z min-only 金字塔无法安全跳过朝相机方向的射线；该方向、投影不足一个像素和异常远端投影现在统一回退到 Pixel DDA，不再回退到会产生世界步长投影条带的 Fixed Step。

### Hi-Z 与 Pixel DDA 的等价边界

- Hi-Z 可以做到“在覆盖率上不差于 Pixel DDA”，但前提是每个 coarse skip 都有保守证明、mip0 与 DDA 共用完全相同的裁剪和叶级命中 oracle，并且遍历预算足够；它不是仅凭“基于 DDA”就天然具有的无条件性质。
- 当前远离相机方向使用 forward-Z 最小深度做保守跳空，并在叶节点复用 DDA 的同格交叉、共享边 continuity bridge 与角点 supercover；朝相机方向因 min-only 数据不足而直接走完整 Pixel DDA。因此在默认 4096 预算和当前分辨率下，两条路径应具有相同的实用命中语义，当前人工对照也观察到一致结果。
- 当前实现仍不承诺任意参数下的绝对/逐位一致：Hi-Z 与 DDA 统计的是不同工作量，人工把预算调得过小时 Hi-Z 会直接返回 `MISS_ITERATIONS`；浮点 cell-boundary 归属、层级归一化和 epsilon 也可能在极端边界产生差异。
- 若未来需要严格的“预算不足时覆盖也不差于 DDA”契约，应在层级遍历出现不确定性或耗尽预算时，从“最后一个已证明为空的位置”启动 suffix/range DDA，而不是从射线起点重跑完整 DDA。这样正常路径仍保留 Hi-Z 加速，只有罕见尾段承担 DDA 成本；suffix 还必须继承前一叶节点/边界上下文，并获得足够预算。
- 即便满足上述等价契约，二者也只能对当前单层屏幕 snapshot 等价，不能补出屏外、背面、被遮挡表面或透明物体，因此不等价于完整平面镜反射。若要让朝相机方向也由层级结构安全加速，需要把当前 `R32F` min-only 金字塔升级为同时保存区间上下界的 min/max 层级。

## 分阶段验收

- 固定步进版：用于观察 view-space 固定步长的量化、覆盖范围和教学对照。
- Pixel DDA 版：平面水面下命中区域应比固定步进连续；Hit UV 应平滑，长射线预算不足时只出现可解释的黄色区域，且不会出现固定世界步长造成的规则采样带。
- Hi-Z 版：在同一相机与参数下，hit mask 和 hit UV 应与 Pixel DDA 基线大体一致；差异必须可通过 debug view 定位。
- Fixed / Pixel DDA / Hi-Z 三种 trace 路径都必须通过 `cmd /c .\bat\cmake-debug-build.bat` 编译，并确认生成 `08_ssr.exe`。
- 当前阶段不设置硬性性能门槛；保留 FPS 与 profile 数据，由使用者在相同场景下直接观察。性能优化和量化目标在功能正确后单独处理。

## 运行时模式切换

- 定义统一的 SSR 模式枚举：`Off = 0`、`FixedStep = 1`、`PixelDDA = 2`、`HiZ = 3`。
- ImGui 使用单选项或下拉框切换模式，不通过重新编译 shader 选择算法。
- `Off` 保留非 SSR 水面作为视觉基线；`FixedStep`、`PixelDDA` 和 `HiZ` 共用最大距离、原点偏移、边缘淡出与颜色合成，只有 Fixed Step 使用可调 thickness 和二分步数。
- 公共 profiler 始终记录 Water 的 CPU/GPU 区段，Hi-Z 模式另外记录 `Hi-Z Build`；可在完全相同的场景和相机下配合 FPS 比较性能。
- 切换模式不得重新创建与窗口尺寸无关的资源；Hi-Z 模式可按需构建深度金字塔。

## 调试视图

- ImGui 提供统一 `Debug View` 选择，至少包含：
  - `Final Composite`
  - `SSR Only`
  - `Linear Scene Depth`
  - `Hit Mask / Confidence`
  - `Hit UV`
  - `Iteration Count`
- Hi-Z 模式额外提供访问 mip 层级的可视化；可显示最终命中层级或遍历中的最大层级。
- 三种 trace 路径使用相同的命中结果结构与颜色编码，保证截图和逐帧对照有效。
- `Hit Mask / Confidence` 必须区分有效命中、越界、达到最大距离、耗尽迭代和深度拒绝，至少能通过颜色或 UI 图例识别原因。
- 当前颜色约定为：红色表示屏幕/投影路径退出，橙色表示到达最大距离，黄色表示迭代预算耗尽，洋红表示最终未命中且遍历过程中遇到过没有形成符号 bracket、连续性检查失败或解析失败的深度候选。DDA 的洋红是“沿途事件”汇总，不应解读为射线恰好终止在该像素。
- `Iteration Count` 的红通道是 `steps / Max Steps`，绿色是 `maxMip / HiZMaxMip`；Pixel DDA 绿色为 0，Hi-Z 绿色显示层级遍历访问过的最大 mip。朝相机方向因 min-only 金字塔限制而直接走 DDA 的射线没有层级前缀，因此绿色同样为 0。
- 调试视图只改变最终显示，不改变 ray marching、深度金字塔或颜色合成输入，避免观察模式影响被测算法。

## 水面颜色合成

- 折射颜色来自波纹扰动后的只读 HDR 场景颜色；若扰动 UV 对应的场景深度位于水面之前，则撤销该次扰动，避免前景颜色被错误折射。
- 根据水面与场景深度的视空间距离计算水深，用吸收颜色/渐变调制折射。
- SSR 命中颜色与环境回退按 `confidence` 混合，再叠加由扰动法线计算的直接光镜面高光。
- 水面直接光高光使用与 PBR 物体相同的 Directional Light 颜色和强度，不使用写死的白色光照。
- 使用 Schlick Fresnel：默认水面 `F0` 取约 `0.02`，并通过 ImGui 暴露必要参数。
- 最终采用 `refraction * (1 - F) + reflection * F`，避免无约束加法造成能量增亮。
- 提供 `Final Composite` 与 `SSR Only` 输出，后者用于验证 ray marching，不参与最终画面验收。

## 动态水面参数

- 每帧上传 `glfwGetTime()`，因此水面不是静态法线贴图；两层波纹分别滚动后叠加为切线空间扰动法线。
- 这里的“水面会动”指波纹采样、法线、折射与反射随时间流动，水面网格本身保持平面，不做顶点高度位移。
- `Wave Global Speed` 统一缩放两层动画速度，范围为 `[0, 10]`；设为 `0` 可冻结水面，便于逐帧排查 SSR。
- `Wave Scale 1/2` 范围为 `[0, 20]`，上传后乘以 `0.01` 转换为世界空间纹理频率。默认值 `2` 和 `10` 分别对应 `0.02` 和 `0.1`。
- `Wave Speed 1/2` 分别控制两层纹理的二维流向与速度；本实现默认使用非零、不同速度，以保证启动即有动态且减少重复图案。
- 两层 `Wave Scale` 均接入 uniform，UI 调整会立即改变对应的世界空间纹理频率。
- `Flat Water (Test)` checkbox 用于排除动态波纹对 SSR 的干扰。开启后 shader 不计算时间滚动 UV、不采样波纹纹理，并直接使用水面几何法线；速度、Scale、Wave Strength 和折射扰动均不参与结果。
- 平面测试开启时 UI 隐藏全部波纹及折射扰动控件和波纹纹理预览，并触发当前 ImGui 控制窗口重新 auto-fit；关闭时恢复此前保存的动态参数和完整控件。

## FPS 相机

- `08-ssr` 直接使用 `src/common/camera/fps_camera.hpp`，不再使用环绕目标的 `ModelViewerCamera`。
- 每帧按 `delta time` 调用 `FPSCamera::update()`，并将键盘、鼠标按键、光标和滚轮事件转发给相机。
- 帧间隔上限钳制为 `0.1s`，避免断点或窗口拖动后一次更新造成相机跳跃。
- ImGui 请求键盘或鼠标焦点时不启动相机操作；释放事件仍会转发，防止按键或旋转状态卡住。
- 禁用相机控制时主动清空移动键和鼠标拖动状态；即使焦点在 UI 上，释放事件也不会留下持续移动。
- SSR 的 near/far 裁剪参数同步为 FPSCamera 当前投影使用的 `0.1/200.0`。

## 核心推导与设计取舍

- 视空间反射射线写作 `X(t) = O + tD`。先按 `Max Distance`、near/far 平面得到有限端点，再分别投影为齐次坐标 `H0 = P * vec4(X0, 1)` 与 `H1 = P * vec4(X1, 1)`。
- 令屏幕像素坐标为 `P0/P1`，并保存 `Q0 = X0 / H0.w`、`Q1 = X1 / H1.w` 以及 `k0 = 1 / H0.w`、`k1 = 1 / H1.w`。沿投影线段使用同一个屏幕参数 `λ` 线性插值 `P/Q/k`，即可用 `X(λ) = mix(Q0, Q1, λ) / mix(k0, k1, λ)` 恢复透视正确的视空间位置；直接线性插值 view position 会产生透视误差。
- viewport slab 裁剪同样作用于 `λ`，因此裁剪 `P` 的同时必须用相同参数裁剪 `Q/k`。DDA 随后精确推进到下一条 X/Y 像素边界，使每个访问区间只属于一个 texel，而不是依赖会随透视变化的世界步长。
- OpenGL 当前使用 forward-Z；重建后统一以正的线性深度 `rayDepth = -X.z`、`sceneDepth = -scenePosition.z` 比较。`rayDepth - sceneDepth` 在一个连续区间内发生任一方向换号，才表示射线与记录表面形成 bracket；正到负和负到正都必须处理。
- 单层深度在 texel 间是离散阶梯。若零点恰好落在共享边，任何一个 cell 内都可能没有严格换号，因此需要保存前一格，并在两份深度通过连续性阈值后把共享边作为稳定交点。角点同时跨越 X/Y 时还需检查两个 side texel，形成二维 supercover，避免漏掉或跨过断层。
- forward-Z 深度金字塔每个 coarse cell 保存最小深度，也就是该区域最靠近相机的表面。只有当射线区间在 cell 出口仍严格位于这个最小深度之前时，才能证明它位于该区域全部几何之前并安全跳过；否则必须下降到更细 mip。金字塔只负责证明空区间，最终命中始终交给 mip0 的 DDA oracle。
- 当射线深度朝相机减小时，min-only 层级缺少证明安全跳过所需的另一侧深度界，因此直接使用 Pixel DDA。若要双向层级加速，需要让每个 cell 同时保存 min/max 深度区间。
- Fixed Step 的 thickness 是 view-Z 容差，并不代表真实表面厚度；掠射角下对应的沿射线容差会被放大。Pixel DDA/Hi-Z 因此使用符号 bracket、解析根和随深度缩放的数值 epsilon，而不靠放大 thickness 填洞。
- 水面最终使用 Schlick Fresnel 在折射与反射之间分配能量：`C = (1 - F) * C_refract + F * C_reflect`。SSR miss 或低置信度区域将 `C_reflect` 回退到环境光颜色；直接光镜面项独立加入反射侧。

## 已完成实施顺序

1. 修复场景、深度与 framebuffer 基线。
2. 实现场景颜色/深度快照和完整水面合成。
3. 完成固定步进、二分细化及全部 debug view，作为第一阶段功能验证点。
4. 构建 `R32F` 深度金字塔并完成 forward-Z Hi-Z 遍历。
5. 根据 Flat Water 的 Hit Mask 结果补充严格 1-pixel DDA，并将 Hi-Z 不安全方向切换为 DDA fallback。
6. 接入 `Off / Fixed Step / Pixel DDA / Hi-Z` UI 切换，编译并进行对照验证。
7. 根据 Pixel DDA 的规则白色空洞补充断层感知的跨 cell continuity bracket，避免使用会跨越 silhouette 的超大单侧 thickness。
8. 删除不适合作为正确性 oracle 的宽松单点 ray-march 临时模式；将 DDA 改为双向同格 bracket、直接共享边 bridge 与角点 supercover；Hi-Z 先以完整 DDA 锁定结果一致性，再将 mip0 确认收缩为直接复用上述叶级 oracle，消除从起点重复扫描的性能倒挂。
9. 功能验收后再根据 FPS/Profile 单独开展性能优化。

## 验证记录

- `glslangValidator`：`water.vert + water.frag` 联合链接通过，`Transform` UBO 为 320 bytes，`Params` UBO 为 192 bytes；`depth_pyramid.comp` 编译通过。
- Windows Debug 全量构建通过，使用 `.agents/AGENTS.md` 规定的 `bat/cmake-debug-build.bat`，已生成 `out/build/x64-Debug/src/08-ssr/Debug/08_ssr.exe`。
- 固定步进模式已进行可见运行检查；相隔 1.2 秒的水面反射采样区域中，15,600 个像素有 4,057 个发生有效变化（26.01%），确认默认非零参数下波纹确实随时间运动。
- Hi-Z 模式使用隐藏窗口进行了 10 帧非交互 smoke test，深度金字塔和 Hi-Z 水面路径实际执行，进程正常以 exit code 0 结束。
- Pixel DDA 增量通过 `glslangValidator` 联合链接，UBO 仍为 `Transform = 320 bytes`、`Params = 192 bytes`；随后通过 `bat/cmake-debug-build.bat` 完整 Debug 构建并生成 `08_ssr.exe`。
- 断层感知的跨 cell continuity bridge 再次通过 `glslangValidator` 联合链接和 `bat/cmake-debug-build.bat` 全量 Debug 构建；本轮未自动启动窗口，留待同机位的 `SSR Only / Hit Mask` 人工对照。
- 删除宽松单点 ray-march 临时模式后，双向 DDA、共享边/角点修复以及 Hi-Z 的统一叶级 oracle 再次通过 `glslangValidator`；`depth_pyramid.comp` 也独立编译通过。此前因人工检查中的 `08_ssr.exe` 锁定输出而出现过 `LNK1168`，关闭窗口并重新构建后，新二进制 UI 已确认只保留四个正式模式。
- Hi-Z 性能修复移除了 mip0 候选与预算耗尽处的全路径 DDA 重跑；叶级 `Q/k` 同格测试、解析式入口边界与假候选继续遍历通过 `glslangValidator` 联合链接，并再次通过 `bat/cmake-debug-build.bat` 全量 Debug 构建生成 `08_ssr.exe`。本轮未自动启动窗口，FPS 与同机位 DDA/Hi-Z 图像对照留给人工验收。
- Fixed Step 可修问题完成后，`water.vert + water.frag` 再次通过 `glslangValidator` 联合链接；使用 `.agents/AGENTS.md` 规定的 PATH 去重兼容入口调用 `bat/cmake-debug-build.bat`，全量 Debug 构建成功并生成 `out/build/x64-Debug/src/08-ssr/Debug/08_ssr.exe`。随后对新进程执行 3 秒隐藏运行 smoke test，运行时 shader 创建和主循环未提前退出；最终画面差异仍留给交互式同机位检查。
- 当前默认切换为 `Pixel DDA (1 px)`，用于直接复查平面水面的反射连续性；Fixed Step 与 Hi-Z 均可在 UI 中即时切回。

## Flat Water 辅助图判读

- 用户提供的 Fixed Step `Hit Mask / Confidence` 中，大片红色表示反射射线确实从屏幕边界离开；这部分属于 SSR 只能访问当前屏幕内容的固有限制，DDA 不会凭空补出屏幕外信息。
- 物体镜像轮廓附近集中出现的洋红色表示首个深度候选在细化/厚度检查中被拒绝；它与固定 view-space 步长跨过多个投影像素、跨 silhouette 建立无效 bracket 相符。
- Pixel DDA 的目标是消除第二类规则漏采样和跨 texel bracket。若 DDA 下仍为红色，属于 screen-space coverage；若变为黄色，则应提高 `Max Steps`；若仍有少量洋红，表示整条未命中射线上曾遇到过深度候选但无法证明合法的双向符号交叉，应检查单层深度的 silhouette/disocclusion 限制，而不是调节对 DDA 已无作用的 `Thickness`，也不能把洋红位置当成精确终点。
- 最新同机位对比中，Pixel DDA 的反射区域出现规则白/洋红扫描线，而 Hi-Z 基本连续。原因是旧 DDA 在已经确认 front-to-back 交叉后，仍要求 cell 出口的 view-Z 穿透量不超过 `Thickness = 0.03`；掠射光线跨一个屏幕像素时经常超过该值，于是有效交叉被周期性拒绝。现已删除这个出口 overshoot 上限并改为解析根加数值 epsilon；预期规则洋红扫描线消失，剩余缺口只反映屏幕边界、单层深度和轮廓不连续。
- 删除 overshoot 上限后的 `SSR Only` 仍在底座斜面上显示规则白条。二维 DDA 并未漏格；真正原因是每格中心 depth 构成阶梯：格 A 出口仍在 A 深度前，格 B 的 depth 跳变又让入口已经落到 B 深度后，严格同 cell 规则使两格都不包含零点。相邻水面 ray 与像素网格的规则相位变化最终表现为条纹。
- 当前修复对共享边上的任一方向符号跳跃尝试 continuity bridge，对同时跨 X/Y 的角点使用 side-texel supercover，并通过自动像素 footprint 阈值拒绝深度断层；这针对底座等连续斜面。若 Hit Mask 仍在物体轮廓出现洋红，表示 continuity gate 正在保守拒绝单层深度无法证明的交点。
- 最新 Pixel DDA Final Composite 中，木底座下方到头盔镜像之间的纵向拉丝不是 Y 坐标或 `P/Q/k` 缩放错误。当前相机位于水面上方，而平面镜等效相机位于水面下方；真实镜像需要头盔底部、背面和遮挡后的表面，主相机 snapshot 只保存其可见最前层。大量近似竖直的水面 ray 因而反复遇到同一圈 silhouette depth，产生典型单层 SSR disocclusion 拉丝。
- 连续斜面上的规则环境色条纹属于深度离散问题，应由 continuity bridge 修复；物体轮廓、透明镜片、背面和遮挡后的环境色缺口则来自 snapshot 中不存在的表面。仅靠当前单层 color/depth 无法得到完全完整的平面镜像。

## 当前限制

- snapshot 只包含不透明 PBR 场景；透明物体在水面之后绘制，因此当前不会出现在 SSR 命中颜色中。
- Hi-Z 使用 forward-Z min-only 金字塔；纹理空间深度朝相机移动的射线会回退到 Pixel DDA，以避免重新引入固定 view-space 步长的屏幕条带。
- SSR miss 只使用环境光颜色，没有 cubemap、天空盒或粗糙度颜色 mip。
- Fixed Step 已修复单向 bracket、伪 bracket、首个候选失败即终止、尾段漏采和预算/调试显示问题；但固定 view-space 步长仍可能完整跨过薄物体交叉区间，固定 view-Z thickness 仍具有视角/深度依赖，不能作为完整正确性 oracle。
- Pixel DDA 仍受单层当前视角深度、屏幕边界、遮挡面和透明物体不在 snapshot 中等 SSR 固有限制；完整平面镜像仍应使用 planar reflection，而不是继续放宽 thickness。
- 当前 FlightHelmet 的透明镜片在 water 之后绘制，不进入 opaque snapshot；其镜像区域必然缺失或回退环境。若后续目标改为完整平面水镜，应增加镜像相机 planar reflection；若仍限定 SSR，只能在“严格拒绝后的缺口”和“放宽命中后的拉丝/漏光”之间取舍。
- exact-cell DDA 的 4096 硬上限覆盖当前 800×600 测试窗口以及最坏约 2560×1440 的屏幕对角遍历；4K 最坏路径可能达到约 6000 个 cell，此时会诚实显示黄色预算耗尽，需要提高 shader 上限或使用 Hi-Z，而不是放大像素 stride。
- 当前没有硬性性能门槛，按约定直接观察 FPS；功能确认后再做 profile 与优化。
