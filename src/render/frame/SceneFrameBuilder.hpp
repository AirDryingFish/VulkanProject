#pragma once

#include "render/RenderTypes.hpp"
#include "render/RenderSettings.hpp"

#include <vector>

class Scene;
class Camera;

// 生成只读绘制视图；返回的 buffer/set 句柄仍由场景资源持有至提交完成。
std::vector<RenderObjectView> buildRenderObjects(const Scene& scene);

// 将场景与显示设置转换成渲染参数；当前帧句柄、阴影计算结果由调用方补齐。
RenderFrameData buildSceneRenderData(const Scene& scene, const Camera& camera, const DisplaySettings& display,
                                     const ShadowSettings& shadows);
