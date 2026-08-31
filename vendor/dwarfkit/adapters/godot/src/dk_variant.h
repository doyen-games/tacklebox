// json <-> Variant conversion (BLUEPRINT.md 8.2), the one marshalling point
// between dwarfkit and Godot.
#pragma once

#include <godot_cpp/variant/variant.hpp>

#include <dwarfkit/core/json.hpp>

namespace dwarfkit_godot {

godot::Variant JsonToVariant(const dwarfkit::json& value);
dwarfkit::json VariantToJson(const godot::Variant& value);

godot::String ToGodot(const std::string& value);
std::string FromGodot(const godot::String& value);

}  // namespace dwarfkit_godot
