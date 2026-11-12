#pragma once

#include "assets/gltf/GltfImportTypes.hpp"
#include "scene/MeshData.hpp"

#include <string>

// 仅生成 CPU 顶点、索引与包围盒，不创建 GPU 资源。OBJ 路径由调用方先应用默认配置。
[[nodiscard]] MeshBuildData buildMeshData(MeshSource source, const std::string& path);

// 将已验证的 glTF primitive 转为项目顶点布局，复制数据并保留切线和包围盒状态。
[[nodiscard]] MeshBuildData buildGltfPrimitiveMeshData(const GltfPrimitiveData& primitive);
