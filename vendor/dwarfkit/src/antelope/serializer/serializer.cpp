// Port of the dynamic (json + type name + ABI) paths of antelope
// src/serializer/encoder.ts (encodeAny) and decoder.ts (decodeBinary,
// decodeObject).
#include <set>

#include <dwarfkit/antelope/serializer.hpp>

#include <dwarfkit/antelope/chain/abi.hpp>
#include <dwarfkit/antelope/serializer/builtins.hpp>

namespace dwarfkit {

namespace {

using Node = ABI::ResolvedNode;

struct PathEntry {
    std::string field;  // name or stringified index
    bool isIndex = false;
    const Node* type = nullptr;
};

struct Context {
    std::vector<PathEntry> codingPath;
    bool strictExtensions = false;

    std::string path() const {
        std::string rv;
        for (size_t i = 0; i < codingPath.size(); i++) {
            if (i) rv += ".";
            if (codingPath[i].isIndex) {
                rv += codingPath[i].field;
            } else {
                rv += codingPath[i].field + "<" + codingPath[i].type->typeName() + ">";
            }
        }
        return rv;
    }

    std::string fieldPath() const {
        std::string rv;
        for (size_t i = 0; i < codingPath.size(); i++) {
            if (i) rv += ".";
            rv += codingPath[i].field;
        }
        return rv;
    }
};

std::string jsValue(const json& v) {
    // interpolation of the offending value in upstream error strings
    if (v.is_null()) return "null";
    if (v.is_string()) return v.get<std::string>();
    return v.dump();
}

// ---- encodeAny -------------------------------------------------------------

Result<void> encodeAny(const json& value, const Node& type, ABIEncoder& encoder, Context& ctx);

Result<void> encodeInner(const json& value, const Node& type, ABIEncoder& encoder, Context& ctx) {
    const auto& types = builtinTypes();
    const auto abiType = types.find(type.name);
    const bool valueExists = !value.is_null();
    if (type.ref && abiType == types.end()) {
        // type is alias, follow it
        return encodeAny(value, *type.ref, encoder, ctx);
    }
    if (!valueExists) {
        if (type.isExtension) {
            return {};
        }
        return err(ErrorKind::Invalid, "Found " + jsValue(value) + " for non-optional type: " +
                                           type.typeName() + " (" + ctx.fieldPath() + ")");
    }
    if (abiType != types.end() && !type.fields && !type.variant) {
        return abiType->second.encodeJSON(value, encoder);
    }
    if (type.fields) {
        if (!value.is_object()) {
            return err(ErrorKind::Invalid, "Expected object for: " + type.name);
        }
        const auto fields = type.allFields();
        if (!fields) {
            return err(ErrorKind::Invalid, "Invalid struct fields");
        }
        for (const auto& [name, fieldType] : *fields) {
            ctx.codingPath.push_back({name, false, fieldType});
            const json& fieldValue = value.contains(name) ? value.at(name) : json(nullptr);
            DK_CHECK(encodeAny(fieldValue, *fieldType, encoder, ctx));
            ctx.codingPath.pop_back();
        }
        return {};
    }
    if (type.variant) {
        std::string vName;
        json inner = value;
        if (value.is_array() && value.size() == 2 && value[0].is_string()) {
            vName = value[0].get<std::string>();
            inner = value[1];
        } else if (value.is_string()) {
            vName = "string";
        } else if (value.is_boolean()) {
            vName = "bool";
        } else {
            vName = "undefined";
        }
        int vIdx = -1;
        for (size_t i = 0; i < type.variant->size(); i++) {
            if ((*type.variant)[i]->typeName() == vName) {
                vIdx = static_cast<int>(i);
                break;
            }
        }
        if (vIdx < 0) {
            std::string names;
            for (size_t i = 0; i < type.variant->size(); i++) {
                if (i) names += ", ";
                names += "'" + (*type.variant)[i]->typeName() + "'";
            }
            return err(ErrorKind::Invalid,
                       "Unknown variant type '" + vName + "', expected one of " + names);
        }
        const Node* vType = (*type.variant)[static_cast<size_t>(vIdx)];
        encoder.writeVaruint32(static_cast<uint32_t>(vIdx));
        ctx.codingPath.push_back({"v" + std::to_string(vIdx), false, vType});
        DK_CHECK(encodeAny(inner, *vType, encoder, ctx));
        ctx.codingPath.pop_back();
        return {};
    }
    if (abiType == types.end()) {
        return err(ErrorKind::Invalid,
                   type.name == "any" ? "Unable to encode any type to binary" : "Unknown type");
    }
    return abiType->second.encodeJSON(value, encoder);
}

Result<void> encodeAny(const json& value, const Node& type, ABIEncoder& encoder, Context& ctx) {
    const bool valueExists = !value.is_null();
    if (type.isOptional) {
        encoder.writeByte(valueExists ? 1 : 0);
        if (!valueExists) {
            return {};
        }
    }
    if (type.isArray) {
        if (!value.is_array()) {
            return err(ErrorKind::Invalid, "Expected array for: " + type.typeName());
        }
        if (!type.size) {
            encoder.writeVaruint32(static_cast<uint32_t>(value.size()));
        }
        for (size_t i = 0; i < value.size(); i++) {
            ctx.codingPath.push_back({std::to_string(i), true, &type});
            DK_CHECK(encodeInner(value[i], type, encoder, ctx));
            ctx.codingPath.pop_back();
        }
        return {};
    }
    return encodeInner(value, type, encoder, ctx);
}

// ---- defaultValue (strictExtensions synthesis) -----------------------------

Result<json> defaultValue(const Node& type, Context& ctx, std::set<std::string>& seen) {
    if (type.isArray) {
        return json::array();
    }
    if (type.isOptional) {
        return json(nullptr);
    }
    const auto& types = builtinTypes();
    const auto abiType = types.find(type.name);
    if (abiType != types.end() && !type.fields && !type.variant && !type.ref) {
        return abiType->second.defaultJSON();
    }
    if (seen.count(type.name)) {
        return err(ErrorKind::Invalid, "Circular type reference");
    }
    seen.insert(type.name);
    if (type.fields) {
        const auto fields = type.allFields();
        if (!fields) {
            return err(ErrorKind::Invalid, "Invalid struct fields");
        }
        json rv = json::object();
        for (const auto& [name, fieldType] : *fields) {
            ctx.codingPath.push_back({name, false, fieldType});
            DK_TRY(fieldDefault, defaultValue(*fieldType, ctx, seen));
            rv[name] = std::move(fieldDefault);
            ctx.codingPath.pop_back();
        }
        return rv;
    }
    if (type.variant && !type.variant->empty()) {
        // upstream defaults to the first alternative with a fresh seen set
        std::set<std::string> variantSeen;
        DK_TRY(inner, defaultValue(*(*type.variant)[0], ctx, variantSeen));
        return json::array({(*type.variant)[0]->typeName(), std::move(inner)});
    }
    if (type.ref) {
        ctx.codingPath.push_back({"", false, type.ref});
        auto rv = defaultValue(*type.ref, ctx, seen);
        ctx.codingPath.pop_back();
        return rv;
    }
    return err(ErrorKind::Invalid, "Unable to determine default value");
}

Result<json> defaultValue(const Node& type, Context& ctx) {
    std::set<std::string> seen;
    return defaultValue(type, ctx, seen);
}

// ---- decodeBinary ----------------------------------------------------------

Result<json> decodeBinary(const Node& type, ABIDecoder& decoder, Context& ctx);

Result<json> decodeBinaryInner(const Node& type, ABIDecoder& decoder, Context& ctx) {
    const auto& types = builtinTypes();
    const auto abiType = types.find(type.name);
    if (abiType != types.end() && !type.fields && !type.variant && !type.ref) {
        return abiType->second.decodeABI(decoder);
    }
    if (type.ref) {
        // follow type alias
        ctx.codingPath.push_back({"", false, type.ref});
        auto rv = decodeBinary(*type.ref, decoder, ctx);
        ctx.codingPath.pop_back();
        return rv;
    }
    if (type.fields) {
        const auto fields = type.allFields();
        if (!fields) {
            return err(ErrorKind::Invalid, "Invalid struct fields");
        }
        json rv = json::object();
        for (const auto& [name, fieldType] : *fields) {
            ctx.codingPath.push_back({name, false, fieldType});
            DK_TRY(fieldValue, decodeBinary(*fieldType, decoder, ctx));
            rv[name] = std::move(fieldValue);
            ctx.codingPath.pop_back();
        }
        return rv;
    }
    if (type.variant) {
        DK_TRY(vIdx, decoder.readByte());
        if (vIdx >= type.variant->size()) {
            return err(ErrorKind::Invalid, "Unknown variant idx: " + std::to_string(vIdx));
        }
        const Node* vType = (*type.variant)[vIdx];
        ctx.codingPath.push_back({"v" + std::to_string(vIdx), false, vType});
        DK_TRY(inner, decodeBinary(*vType, decoder, ctx));
        ctx.codingPath.pop_back();
        return json::array({vType->typeName(), std::move(inner)});
    }
    if (abiType != types.end()) {
        return abiType->second.decodeABI(decoder);
    }
    return err(ErrorKind::Invalid,
               type.name == "any" ? "Unable to decode 'any' type from binary" : "Unknown type");
}

Result<json> decodeBinary(const Node& type, ABIDecoder& decoder, Context& ctx) {
    if (ctx.codingPath.size() > 32) {
        return err(ErrorKind::Invalid, "Maximum decoding depth exceeded");
    }
    if (type.isExtension && !decoder.canRead()) {
        if (ctx.strictExtensions) {
            return defaultValue(type, ctx);
        }
        return json(nullptr);
    }
    if (type.isOptional) {
        DK_TRY(present, decoder.readByte());
        if (present == 0) {
            return json(nullptr);
        }
    }
    if (type.isArray) {
        uint32_t len = 0;
        if (type.size) {
            len = *type.size;
        } else {
            DK_TRY(varLen, decoder.readVaruint32());
            len = varLen;
        }
        json rv = json::array();
        for (uint32_t i = 0; i < len; i++) {
            ctx.codingPath.push_back({std::to_string(i), true, &type});
            DK_TRY(item, decodeBinaryInner(type, decoder, ctx));
            rv.push_back(std::move(item));
            ctx.codingPath.pop_back();
        }
        return rv;
    }
    return decodeBinaryInner(type, decoder, ctx);
}

// ---- decodeObject ----------------------------------------------------------

Result<json> decodeObject(const json& value, const Node& type, Context& ctx);

Result<json> decodeObjectInner(const json& value, const Node& type, Context& ctx) {
    const auto& types = builtinTypes();
    const auto abiType = types.find(type.name);
    if (type.ref && abiType == types.end()) {
        // follow type alias
        return decodeObject(value, *type.ref, ctx);
    }
    if (type.fields) {
        if (!value.is_object()) {
            return err(ErrorKind::Invalid, "Expected object");
        }
        const auto fields = type.allFields();
        if (!fields) {
            return err(ErrorKind::Invalid, "Invalid struct fields");
        }
        json rv = json::object();
        for (const auto& [name, fieldType] : *fields) {
            ctx.codingPath.push_back({name, false, fieldType});
            const json& fieldValue = value.contains(name) ? value.at(name) : json(nullptr);
            DK_TRY(decoded, decodeObject(fieldValue, *fieldType, ctx));
            rv[name] = std::move(decoded);
            ctx.codingPath.pop_back();
        }
        return rv;
    }
    if (type.variant) {
        std::string vName;
        json inner = value;
        if (value.is_array() && value.size() == 2 && value[0].is_string()) {
            vName = value[0].get<std::string>();
            inner = value[1];
        } else if (value.is_string()) {
            vName = "string";
        } else if (value.is_boolean()) {
            vName = "bool";
        } else {
            vName = "undefined";
        }
        int vIdx = -1;
        for (size_t i = 0; i < type.variant->size(); i++) {
            if ((*type.variant)[i]->typeName() == vName) {
                vIdx = static_cast<int>(i);
                break;
            }
        }
        if (vIdx < 0) {
            return err(ErrorKind::Invalid, "Unknown variant type: " + vName);
        }
        const Node* vType = (*type.variant)[static_cast<size_t>(vIdx)];
        ctx.codingPath.push_back({"v" + std::to_string(vIdx), false, vType});
        DK_TRY(decoded, decodeObject(inner, *vType, ctx));
        ctx.codingPath.pop_back();
        return json::array({vType->typeName(), std::move(decoded)});
    }
    if (abiType == types.end()) {
        // special case for `any` when decoding from object
        if (type.name == "any") {
            return value;
        }
        return err(ErrorKind::Invalid, "Unknown type");
    }
    return abiType->second.normalizeJSON(value);
}

Result<json> decodeObject(const json& value, const Node& type, Context& ctx) {
    if (value.is_null()) {
        if (type.isOptional) {
            return json(nullptr);
        }
        if (type.isExtension) {
            if (ctx.strictExtensions) {
                return defaultValue(type, ctx);
            }
            return json(nullptr);
        }
        return err(ErrorKind::Invalid, "Unexpectedly encountered " + jsValue(value) +
                                           " for non-optional (" + ctx.fieldPath() + ")");
    }
    if (type.isArray) {
        if (!value.is_array()) {
            return err(ErrorKind::Invalid, "Expected array");
        }
        json rv = json::array();
        for (size_t i = 0; i < value.size(); i++) {
            ctx.codingPath.push_back({std::to_string(i), true, &type});
            DK_TRY(item, decodeObjectInner(value[i], type, ctx));
            rv.push_back(std::move(item));
            ctx.codingPath.pop_back();
        }
        return rv;
    }
    return decodeObjectInner(value, type, ctx);
}

tl::unexpected<Error> wrap(const char* kind, const Context& ctx, Error inner) {
    inner.message =
        std::string(kind) + " error at " + ctx.path() + ": " + inner.message;
    return err(std::move(inner));
}

}  // namespace

namespace Serializer {

Result<Bytes> encode(const json& object, std::string_view type, const ABI& abi) {
    const auto rootType = abi.resolveType(std::string(type));
    ABIEncoder encoder;
    Context ctx;
    ctx.codingPath.push_back({"root", false, rootType.get()});
    auto status = encodeAny(object, *rootType, encoder, ctx);
    if (!status) {
        return wrap("Encoding", ctx, std::move(status.error()));
    }
    return encoder.getBytes();
}

Result<json> decode(std::span<const uint8_t> data, std::string_view type, const ABI& abi,
                    const DecodeOptions& options) {
    const auto rootType = abi.resolveType(std::string(type));
    ABIDecoder decoder(data);
    decoder.strictExtensions = options.strictExtensions;
    Context ctx;
    ctx.strictExtensions = options.strictExtensions;
    ctx.codingPath.push_back({"root", false, rootType.get()});
    auto rv = decodeBinary(*rootType, decoder, ctx);
    if (!rv) {
        return wrap("Decoding", ctx, std::move(rv.error()));
    }
    return rv;
}

Result<json> decode(std::span<const uint8_t> data, std::string_view type, const ABI& abi) {
    return decode(data, type, abi, DecodeOptions{});
}

Result<json> decode(const Bytes& data, std::string_view type, const ABI& abi) {
    return decode(std::span<const uint8_t>(data.array), type, abi, DecodeOptions{});
}

Result<json> decodeObject(const json& object, std::string_view type, const ABI& abi,
                          const DecodeOptions& options) {
    const auto rootType = abi.resolveType(std::string(type));
    Context ctx;
    ctx.strictExtensions = options.strictExtensions;
    ctx.codingPath.push_back({"root", false, rootType.get()});
    auto rv = dwarfkit::decodeObject(object, *rootType, ctx);
    if (!rv) {
        return wrap("Decoding", ctx, std::move(rv.error()));
    }
    return rv;
}

Result<json> decodeObject(const json& object, std::string_view type, const ABI& abi) {
    return decodeObject(object, type, abi, DecodeOptions{});
}

}  // namespace Serializer

}  // namespace dwarfkit
