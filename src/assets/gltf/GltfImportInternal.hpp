#pragma once

#include "assets/gltf/GltfImportTypes.hpp"

namespace fastgltf
{
class Asset;
}

// 仅供 CPU 导入实现使用；各阶段共享只读解析资产，向结果的对应表追加数据。
namespace gltf_import_detail
{
// 解码来源数据已加载的图片，结果拥有独立 RGBA8 像素。
void decodeImages(const fastgltf::Asset& asset, const std::filesystem::path& normalizedPath, GltfImportData& result);

// 图片表建立后，解码 sampler、texture 引用与材质，并验证表间索引。
void decodeMaterials(const fastgltf::Asset& asset, const std::filesystem::path& normalizedPath, GltfImportData& result);

// 材质表建立后，解码网格与顶点属性，生成缺失法线并验证材质所需 UV。
void decodeMeshes(const fastgltf::Asset& asset, const std::filesystem::path& normalizedPath, GltfImportData& result);
} // namespace gltf_import_detail
