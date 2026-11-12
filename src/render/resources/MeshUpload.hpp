#pragma once

#include "scene/Mesh.hpp"

class Renderer;

// 同批上传顶点与索引，返回拥有 GPU buffer 的网格；成功返回时 staging 已可释放。
Mesh uploadMesh(Renderer& renderer, const MeshBuildData& meshData);
