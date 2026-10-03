module;
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <ImGuizmo.h>
export module imguizmo;

// Usage:
//   ImGuizmo::SetDrawlist();
//   ImGuizmo::SetRect(0, 0, width, height);
//   ImGuizmo::Manipulate(view, projection, ImGuizmo::TRANSLATE, ImGuizmo::LOCAL, modelMatrix);
export namespace ImGuizmo
{
    using ImGuizmo::OPERATION;
    using ImGuizmo::TRANSLATE;
    using ImGuizmo::ROTATE;
    using ImGuizmo::SCALE;
    using ImGuizmo::BOUNDS;
    using ImGuizmo::UNIVERSAL;
    using ImGuizmo::MODE;
    using ImGuizmo::LOCAL;
    using ImGuizmo::WORLD;

    using ImGuizmo::SetDrawlist;
    using ImGuizmo::SetRect;
    using ImGuizmo::SetOrthographic;
    using ImGuizmo::BeginFrame;
    using ImGuizmo::IsOver;
    using ImGuizmo::IsUsing;
    using ImGuizmo::Enable;
    using ImGuizmo::DecomposeMatrixToComponents;
    using ImGuizmo::RecomposeMatrixFromComponents;
    using ImGuizmo::Manipulate;
    using ImGuizmo::ViewManipulate;
    using ImGuizmo::DrawGrid;
    using ImGuizmo::DrawCubes;
    using ImGuizmo::SetGizmoSizeClipSpace;
    using ImGuizmo::AllowAxisFlip;
    using ImGuizmo::SetAxisLimit;
    using ImGuizmo::SetID;
}
