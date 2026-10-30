#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::uint64_t MakeBox(Scene& scene, const std::string& name, const glm::vec3 position = {})
    {
        const auto entity = scene.CreateEntity(name, "Box");
        scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = position});
        return scene.GuidOf(entity);
    }

    glm::vec3 PositionOf(const Scene& scene, const std::uint64_t guid)
    {
        return scene.Registry().get<TransformComponent>(scene.FindByGuid(guid)).Position;
    }

    void ApplyAll(Scene& scene, const std::vector<Command>& commands)
    {
        for (const auto& command : commands) ApplyCommand(scene, command);
    }

    const ComponentTypeInfo& Transform()
    {
        return *ComponentRegistry::Instance().FindByName("Transform");
    }

    const FieldInfo& PositionField()
    {
        return *std::ranges::find(Transform().Fields, "Position", &FieldInfo::Name);
    }
}

TEST_CASE("SampleField reports shared and mixed values", "[clipboard]")
{
    Scene scene("S");
    const auto a = MakeBox(scene, "A", {1, 2, 3});
    const auto b = MakeBox(scene, "B", {1, 2, 3});
    const auto c = MakeBox(scene, "C", {4, 5, 6});

    const std::vector<std::uint64_t> same{a, b};
    const auto shared = SampleField(scene, same, Transform(), PositionField());
    REQUIRE(shared);
    CHECK_FALSE(shared->Mixed);
    CHECK(std::get<glm::dvec3>(shared->Value) == glm::dvec3(1, 2, 3));

    const std::vector<std::uint64_t> differing{a, b, c};
    const auto mixed = SampleField(scene, differing, Transform(), PositionField());
    REQUIRE(mixed);
    CHECK(mixed->Mixed);
}

TEST_CASE("SampleField is empty when an entity lacks the component", "[clipboard]")
{
    Scene scene("S");
    const auto a = MakeBox(scene, "A");
    const auto bare = scene.GuidOf(scene.CreateEntity("Bare", "Box"));
    const std::vector<std::uint64_t> guids{a, bare};
    CHECK_FALSE(AllHaveComponent(scene, guids, Transform()));
    CHECK_FALSE(SampleField(scene, guids, Transform(), PositionField()));
}

TEST_CASE("Multi field edit changes only the edited components and one undo list restores them", "[clipboard]")
{
    Scene scene("S");
    const auto a = MakeBox(scene, "A", {1, 5, 0});
    const auto b = MakeBox(scene, "B", {2, 6, 0});
    const std::vector<std::uint64_t> guids{a, b};

    const auto sample = SampleField(scene, guids, Transform(), PositionField());
    REQUIRE(sample);
    REQUIRE(sample->Mixed);
    FieldValue edited = glm::dvec3(9, 5, 0);

    std::vector<Command> inverses;
    for (const auto& command : MakeSetFieldCommands(scene, guids, Transform(), PositionField(), edited, &sample->Value))
    {
        Command inverse = DestroyEntityCommand{};
        REQUIRE(ApplyCommand(scene, command, &inverse));
        inverses.push_back(inverse);
    }
    CHECK(PositionOf(scene, a) == glm::vec3(9, 5, 0));
    CHECK(PositionOf(scene, b) == glm::vec3(9, 6, 0));

    ApplyAll(scene, inverses);
    CHECK(PositionOf(scene, a) == glm::vec3(1, 5, 0));
    CHECK(PositionOf(scene, b) == glm::vec3(2, 6, 0));
}

TEST_CASE("Multi field edit without a displayed value writes the whole value", "[clipboard]")
{
    Scene scene("S");
    const auto a = MakeBox(scene, "A", {1, 5, 0});
    const auto b = MakeBox(scene, "B", {2, 6, 0});
    const std::vector<std::uint64_t> guids{a, b};
    ApplyAll(scene, MakeSetFieldCommands(scene, guids, Transform(), PositionField(), glm::dvec3(0, 0, 0)));
    CHECK(PositionOf(scene, a) == glm::vec3(0, 0, 0));
    CHECK(PositionOf(scene, b) == glm::vec3(0, 0, 0));
}

TEST_CASE("MergeChangedComponents keeps scalar edits whole", "[clipboard]")
{
    CHECK(std::get<float>(MergeChangedComponents(1.0f, 2.0f, 3.0f)) == 3.0f);
    const auto merged = MergeChangedComponents(glm::vec2(1, 2), glm::vec2(5, 5), glm::vec2(5, 7));
    CHECK(std::get<glm::vec2>(merged) == glm::vec2(1, 7));
}

TEST_CASE("Add and remove component commands skip entities that already match", "[clipboard]")
{
    Scene scene("S");
    const auto a = MakeBox(scene, "A");
    const auto bare = scene.GuidOf(scene.CreateEntity("Bare", "Box"));
    const std::vector<std::uint64_t> guids{a, bare};
    CHECK(MakeAddComponentCommands(scene, guids, Transform()).size() == 1);
    CHECK(MakeRemoveComponentCommands(scene, guids, Transform()).size() == 1);
}

TEST_CASE("Component clipboard text round trips onto other entities", "[clipboard]")
{
    Scene scene("S");
    const auto source = MakeBox(scene, "Source", {7, 8, 9});
    const auto withTransform = MakeBox(scene, "T", {0, 0, 0});
    const auto bare = scene.GuidOf(scene.CreateEntity("Bare", "Box"));

    const auto text = CopyComponentYaml(scene, source, "Transform");
    REQUIRE_FALSE(text.empty());
    const auto content = ParseClipboard(text);
    REQUIRE(content);
    CHECK(content->Kind == ClipboardKind::Component);
    CHECK(content->Component == "Transform");

    const std::vector<std::uint64_t> targets{withTransform, bare};
    ApplyAll(scene, MakePasteComponentCommands(scene, *content, targets));
    CHECK(PositionOf(scene, withTransform) == glm::vec3(7, 8, 9));
    CHECK(PositionOf(scene, bare) == glm::vec3(7, 8, 9));
}

TEST_CASE("Entity clipboard text spawns copies with fresh guids and remapped parents", "[clipboard]")
{
    Scene scene("S");
    const auto parent = MakeBox(scene, "Parent", {1, 1, 1});
    const auto child = MakeBox(scene, "Child", {2, 2, 2});
    REQUIRE(ApplyCommand(scene, SetParentCommand{child, parent}));
    const std::size_t before = scene.Registry().storage<entt::entity>().size();

    const std::vector<std::uint64_t> copied{parent, child};
    const auto text = CopyEntitiesYaml(scene, copied);
    REQUIRE_FALSE(text.empty());
    const auto content = ParseClipboard(text);
    REQUIRE(content);
    REQUIRE(content->Entities.size() == 2);

    std::uint64_t next = 1000;
    std::vector<std::uint64_t> created;
    ApplyAll(scene, MakePasteEntityCommands(scene, *content, [&] { return next++; }, &created));
    REQUIRE(created.size() == 2);
    CHECK(scene.Registry().storage<entt::entity>().size() == before + 2);

    const auto newChild = scene.FindByGuid(created[1]);
    REQUIRE(scene.IsValid(newChild));
    CHECK(scene.Registry().get<HierarchyComponent>(newChild).ParentGuid == created[0]);
    CHECK(PositionOf(scene, created[1]) == glm::vec3(2, 2, 2));
    CHECK(scene.Registry().get<MetadataComponent>(newChild).Name == "Child");
}

TEST_CASE("Pasted entity drops a parent that does not exist in the scene", "[clipboard]")
{
    Scene source("A");
    const auto parent = MakeBox(source, "Parent");
    const auto child = MakeBox(source, "Child");
    REQUIRE(ApplyCommand(source, SetParentCommand{child, parent}));
    const std::vector<std::uint64_t> onlyChild{child};
    const auto content = ParseClipboard(CopyEntitiesYaml(source, onlyChild));
    REQUIRE(content);

    Scene target("B");
    std::uint64_t next = 500;
    std::vector<std::uint64_t> created;
    ApplyAll(target, MakePasteEntityCommands(target, *content, [&] { return next++; }, &created));
    REQUIRE(created.size() == 1);
    CHECK_FALSE(target.Registry().all_of<HierarchyComponent>(target.FindByGuid(created[0])));
}

TEST_CASE("ParseClipboard rejects text that is not a clipboard document", "[clipboard]")
{
    CHECK_FALSE(ParseClipboard(""));
    CHECK_FALSE(ParseClipboard("hello world"));
    CHECK_FALSE(ParseClipboard("Clipboard: Component\nComponent: Nope\nData: {}"));
    CHECK_FALSE(ParseClipboard("Clipboard: Entities\nEntities: []"));
    CHECK_FALSE(ParseClipboard("Clipboard: [unterminated"));
}
