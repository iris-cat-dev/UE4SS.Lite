#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace RC::GUI::Dumpers
{
    auto render() -> void;
    auto call_generate_static_mesh_file() -> void;
    auto call_generate_all_actor_file() -> void;
    auto call_generate_object_as_json(Unreal::UObject*) -> void;
} // namespace RC::GUI::Dumpers
