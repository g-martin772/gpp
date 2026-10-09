module;
#include <imgui.h>
#include <imgui_node_editor.h>
export module node_editor;

export namespace ax::NodeEditor
{
    using ax::NodeEditor::Details::operator==;
    using ax::NodeEditor::Details::operator!=;

    using ax::NodeEditor::EditorContext;
    using ax::NodeEditor::Config;
    using ax::NodeEditor::NodeId;
    using ax::NodeEditor::PinId;
    using ax::NodeEditor::LinkId;
    using ax::NodeEditor::PinKind;
    using ax::NodeEditor::StyleVar;
    using ax::NodeEditor::StyleColor;
    using ax::NodeEditor::FlowDirection;
    using ax::NodeEditor::Style;

    using ax::NodeEditor::CreateEditor;
    using ax::NodeEditor::DestroyEditor;
    using ax::NodeEditor::SetCurrentEditor;
    using ax::NodeEditor::GetCurrentEditor;

    using ax::NodeEditor::PushStyleVar;
    using ax::NodeEditor::PopStyleVar;
    // StyleVar is a plain (unscoped) enum, so its enumerators live directly in ax::NodeEditor rather
    // than nested under StyleVar:: -- each one used by a consumer needs its own using-declaration.
    using ax::NodeEditor::StyleVar_NodePadding;
    using ax::NodeEditor::StyleVar_NodeRounding;
    using ax::NodeEditor::StyleVar_NodeBorderWidth;
    using ax::NodeEditor::StyleVar_HoveredNodeBorderWidth;
    using ax::NodeEditor::StyleVar_SelectedNodeBorderWidth;
    using ax::NodeEditor::StyleVar_HoveredNodeBorderOffset;
    using ax::NodeEditor::StyleVar_SelectedNodeBorderOffset;
    using ax::NodeEditor::StyleVar_PinRadius;
    using ax::NodeEditor::StyleVar_PinRounding;
    using ax::NodeEditor::StyleVar_LinkStrength;
    using ax::NodeEditor::StyleVar_GroupRounding;
    using ax::NodeEditor::StyleVar_GroupBorderWidth;
    using ax::NodeEditor::StyleVar_PivotAlignment;
    using ax::NodeEditor::StyleVar_PivotSize;
    using ax::NodeEditor::StyleColor_Bg;
    using ax::NodeEditor::StyleColor_Grid;
    using ax::NodeEditor::StyleColor_NodeBg;
    using ax::NodeEditor::StyleColor_NodeBorder;
    using ax::NodeEditor::StyleColor_HovNodeBorder;
    using ax::NodeEditor::StyleColor_SelNodeBorder;
    using ax::NodeEditor::StyleColor_PinRect;
    using ax::NodeEditor::StyleColor_PinRectBorder;
    using ax::NodeEditor::StyleColor_Flow;
    using ax::NodeEditor::StyleColor_FlowMarker;
    using ax::NodeEditor::StyleColor_GroupBg;
    using ax::NodeEditor::StyleColor_GroupBorder;
    using ax::NodeEditor::GetStyle;
    using ax::NodeEditor::PushStyleColor;
    using ax::NodeEditor::PopStyleColor;

    using ax::NodeEditor::Begin;
    using ax::NodeEditor::End;
    using ax::NodeEditor::BeginNode;
    using ax::NodeEditor::EndNode;
    using ax::NodeEditor::BeginPin;
    using ax::NodeEditor::EndPin;
    using ax::NodeEditor::Link;
    using ax::NodeEditor::Flow;
    using ax::NodeEditor::Group;
    using ax::NodeEditor::SetGroupSize;
    using ax::NodeEditor::GetNodeSize;
    using ax::NodeEditor::GetNodeBackgroundDrawList;
    using ax::NodeEditor::PinRect;
    using ax::NodeEditor::PinPivotRect;
    using ax::NodeEditor::PinPivotAlignment;
    using ax::NodeEditor::PinPivotSize;
    using ax::NodeEditor::HasAnyLinks;
    using ax::NodeEditor::GetCurrentZoom;
    using ax::NodeEditor::GetHoveredNode;
    using ax::NodeEditor::GetHoveredPin;
    using ax::NodeEditor::GetHoveredLink;
    using ax::NodeEditor::GetLinkPins;
    using ax::NodeEditor::CanvasToScreen;
    using ax::NodeEditor::DeselectNode;
    using ax::NodeEditor::SelectLink;
    using ax::NodeEditor::GetSelectedLinks;
    using ax::NodeEditor::IsNodeSelected;
    using ax::NodeEditor::IsLinkSelected;
    using ax::NodeEditor::ShowNodeContextMenu;
    using ax::NodeEditor::ShowLinkContextMenu;
    using ax::NodeEditor::ShowPinContextMenu;
    using ax::NodeEditor::GetDoubleClickedNode;
    using ax::NodeEditor::SetNodeZPosition;
    using ax::NodeEditor::CenterNodeOnScreen;
    using ax::NodeEditor::DeleteNode;
    using ax::NodeEditor::DeleteLink;
    using ax::NodeEditor::Suspend;
    using ax::NodeEditor::Resume;

    using ax::NodeEditor::BeginCreate;
    using ax::NodeEditor::QueryNewLink;
    using ax::NodeEditor::QueryNewNode;
    using ax::NodeEditor::AcceptNewItem;
    using ax::NodeEditor::RejectNewItem;
    using ax::NodeEditor::EndCreate;

    using ax::NodeEditor::BeginDelete;
    using ax::NodeEditor::QueryDeletedLink;
    using ax::NodeEditor::QueryDeletedNode;
    using ax::NodeEditor::AcceptDeletedItem;
    using ax::NodeEditor::RejectDeletedItem;
    using ax::NodeEditor::EndDelete;

    using ax::NodeEditor::NavigateToContent;
    using ax::NodeEditor::NavigateToSelection;
    using ax::NodeEditor::SetNodePosition;
    using ax::NodeEditor::GetNodePosition;

    using ax::NodeEditor::GetSelectedNodes;
    using ax::NodeEditor::SelectNode;
    using ax::NodeEditor::ClearSelection;
    using ax::NodeEditor::ShowBackgroundContextMenu;
    using ax::NodeEditor::ScreenToCanvas;
}
