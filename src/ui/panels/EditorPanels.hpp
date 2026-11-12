#pragma once

#include "ui/EditorUI.hpp"

// 面板与视口覆盖层的内部入口，共用本次绘制的借用视图。
namespace editor_panels
{
void drawScene(EditorView& view, const EditorActions& actions);
void drawInspector(EditorView& view, const EditorActions& actions);
void drawLighting(EditorView& view, const EditorActions& actions);
void drawShadows(EditorView& view);
void drawCamera(EditorView& view, const EditorActions& actions);
void drawDisplay(EditorView& view);
void drawDiagnostics(EditorView& view);
void drawViewport(EditorView& view);
} // namespace editor_panels
