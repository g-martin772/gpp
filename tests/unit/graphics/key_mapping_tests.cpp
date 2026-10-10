#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

import GPP;
import std;

using namespace GPP;

TEST_CASE("Key codes map to ImGui keys", "[graphics][input]")
{
    CHECK(ToImGuiKey(KeyCode::Delete) == ImGuiKey_Delete);
    CHECK(ToImGuiKey(KeyCode::Num1) == ImGuiKey_1);
    CHECK(ToImGuiKey(KeyCode::F9) == ImGuiKey_F9);
    CHECK(ToImGuiKey(KeyCode::F24) == ImGuiKey_F24);
    CHECK(ToImGuiKey(KeyCode::Home) == ImGuiKey_Home);
    CHECK(ToImGuiKey(KeyCode::Insert) == ImGuiKey_Insert);
    CHECK(ToImGuiKey(KeyCode::Keypad0) == ImGuiKey_Keypad0);
    CHECK(ToImGuiKey(KeyCode::KeypadEnter) == ImGuiKey_KeypadEnter);
    CHECK(ToImGuiKey(KeyCode::LeftControl) == ImGuiKey_LeftCtrl);
    CHECK(ToImGuiKey(KeyCode::RightShift) == ImGuiKey_RightShift);
    CHECK(ToImGuiKey(KeyCode::Grave) == ImGuiKey_GraveAccent);
    CHECK(ToImGuiKey(KeyCode::Unknown) == ImGuiKey_None);
}
