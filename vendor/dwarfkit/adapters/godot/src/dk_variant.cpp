#include "dk_variant.h"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace dwarfkit_godot {

using namespace godot;

String ToGodot(const std::string& value) {
    return String::utf8(value.c_str(), static_cast<int64_t>(value.size()));
}

std::string FromGodot(const String& value) {
    const CharString utf8 = value.utf8();
    return std::string(utf8.get_data(), static_cast<size_t>(utf8.length()));
}

Variant JsonToVariant(const dwarfkit::json& value) {
    switch (value.type()) {
        case dwarfkit::json::value_t::null:
            return Variant();
        case dwarfkit::json::value_t::boolean:
            return Variant(value.get<bool>());
        case dwarfkit::json::value_t::number_integer:
            return Variant(value.get<int64_t>());
        case dwarfkit::json::value_t::number_unsigned:
            // Variant carries int64; larger values round-trip as strings
            return value.get<uint64_t>() <= static_cast<uint64_t>(INT64_MAX)
                       ? Variant(static_cast<int64_t>(value.get<uint64_t>()))
                       : Variant(ToGodot(value.dump()));
        case dwarfkit::json::value_t::number_float:
            return Variant(value.get<double>());
        case dwarfkit::json::value_t::string:
            return Variant(ToGodot(value.get<std::string>()));
        case dwarfkit::json::value_t::array: {
            Array rv;
            for (const auto& item : value) {
                rv.push_back(JsonToVariant(item));
            }
            return rv;
        }
        case dwarfkit::json::value_t::object: {
            Dictionary rv;
            for (const auto& [key, item] : value.items()) {
                rv[ToGodot(key)] = JsonToVariant(item);
            }
            return rv;
        }
        default:
            return Variant();
    }
}

dwarfkit::json VariantToJson(const Variant& value) {
    switch (value.get_type()) {
        case Variant::NIL:
            return dwarfkit::json();
        case Variant::BOOL:
            return dwarfkit::json(static_cast<bool>(value));
        case Variant::INT:
            return dwarfkit::json(static_cast<int64_t>(value));
        case Variant::FLOAT:
            return dwarfkit::json(static_cast<double>(value));
        case Variant::STRING:
        case Variant::STRING_NAME:
            return dwarfkit::json(FromGodot(value));
        case Variant::ARRAY: {
            dwarfkit::json rv = dwarfkit::json::array();
            const Array array = value;
            for (int64_t i = 0; i < array.size(); i++) {
                rv.push_back(VariantToJson(array[i]));
            }
            return rv;
        }
        case Variant::DICTIONARY: {
            dwarfkit::json rv = dwarfkit::json::object();
            const Dictionary dictionary = value;
            const Array keys = dictionary.keys();
            for (int64_t i = 0; i < keys.size(); i++) {
                rv[FromGodot(keys[i])] = VariantToJson(dictionary[keys[i]]);
            }
            return rv;
        }
        default:
            return dwarfkit::json(FromGodot(value.stringify()));
    }
}

}  // namespace dwarfkit_godot
