module;
#include <imgui.h>
#include <imgui_node_editor.h>
export module node_editor;

export namespace ax::NodeEditor
{
    using ax::NodeEditor::EditorContext;
    using ax::NodeEditor::Config;
    using ax::NodeEditor::NodeId;
    using ax::NodeEditor::PinId;
    using ax::NodeEditor::LinkId;
    using ax::NodeEditor::PinKind;

    using ax::NodeEditor::CreateEditor;
    using ax::NodeEditor::DestroyEditor;
    using ax::NodeEditor::SetCurrentEditor;
    using ax::NodeEditor::GetCurrentEditor;

    using ax::NodeEditor::Begin;
    using ax::NodeEditor::End;
    using ax::NodeEditor::BeginNode;
    using ax::NodeEditor::EndNode;
    using ax::NodeEditor::BeginPin;
    using ax::NodeEditor::EndPin;
    using ax::NodeEditor::Link;
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
}
