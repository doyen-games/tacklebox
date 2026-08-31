#include <dwarfkit/antelope/chain/abi.hpp>

#include <map>
#include <set>

#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit {

namespace {

// jsonGet helpers tolerant of missing members (Partial<ABI.Def>)
std::string getString(const json& j, const char* key, std::string fallback = "") {
    if (j.contains(key) && j.at(key).is_string()) return j.at(key).get<std::string>();
    return fallback;
}

}  // namespace

Result<ABI> ABI::from(const json& value) {
    if (value.is_string()) {
        return from(std::string_view(value.get_ref<const std::string&>()));
    }
    if (!value.is_object()) {
        return err(ErrorKind::Invalid, "Invalid ABI definition");
    }
    ABI abi;
    abi.version = getString(value, "version", std::string(defaultVersion));
    if (abi.version.empty()) abi.version = std::string(defaultVersion);
    for (const auto& t : value.value("types", json::array())) {
        abi.types.push_back({getString(t, "new_type_name"), getString(t, "type")});
    }
    for (const auto& v : value.value("variants", json::array())) {
        Variant variant{getString(v, "name"), {}};
        for (const auto& type : v.value("types", json::array())) {
            variant.types.push_back(type.get<std::string>());
        }
        abi.variants.push_back(std::move(variant));
    }
    for (const auto& s : value.value("structs", json::array())) {
        Struct entry{getString(s, "name"), getString(s, "base"), {}};
        for (const auto& f : s.value("fields", json::array())) {
            entry.fields.push_back({getString(f, "name"), getString(f, "type")});
        }
        abi.structs.push_back(std::move(entry));
    }
    for (const auto& a : value.value("actions", json::array())) {
        abi.actions.push_back({Name::from(getString(a, "name")), getString(a, "type"),
                               getString(a, "ricardian_contract")});
    }
    for (const auto& t : value.value("tables", json::array())) {
        Table table{Name::from(getString(t, "name")), getString(t, "index_type"), {}, {},
                    getString(t, "type")};
        for (const auto& k : t.value("key_names", json::array())) {
            table.key_names.push_back(k.get<std::string>());
        }
        for (const auto& k : t.value("key_types", json::array())) {
            table.key_types.push_back(k.get<std::string>());
        }
        abi.tables.push_back(std::move(table));
    }
    for (const auto& c : value.value("ricardian_clauses", json::array())) {
        abi.ricardian_clauses.push_back({getString(c, "id"), getString(c, "body")});
    }
    for (const auto& r : value.value("action_results", json::array())) {
        abi.action_results.push_back(
            {Name::from(getString(r, "name")), getString(r, "result_type")});
    }
    return abi;
}

Result<ABI> ABI::from(std::string_view jsonText) {
    const json parsed = json::parse(jsonText, nullptr, false);
    if (parsed.is_discarded()) {
        return err(ErrorKind::Invalid, "Invalid ABI JSON");
    }
    return from(parsed);
}

Result<ABI> ABI::from(const Blob& value) {
    ABIDecoder decoder(value.array);
    return fromABI(decoder);
}

Result<ABI> ABI::fromABI(ABIDecoder& d) {
    ABI abi;
    DK_TRY(version, d.readString());
    abi.version = std::move(version);
    DK_TRY(numTypes, d.readVaruint32());
    for (uint32_t i = 0; i < numTypes; i++) {
        DK_TRY(newTypeName, d.readString());
        DK_TRY(type, d.readString());
        abi.types.push_back({std::move(newTypeName), std::move(type)});
    }
    DK_TRY(numStructs, d.readVaruint32());
    for (uint32_t i = 0; i < numStructs; i++) {
        DK_TRY(name, d.readString());
        DK_TRY(base, d.readString());
        Struct entry{std::move(name), std::move(base), {}};
        DK_TRY(numFields, d.readVaruint32());
        for (uint32_t j = 0; j < numFields; j++) {
            DK_TRY(fieldName, d.readString());
            DK_TRY(fieldType, d.readString());
            entry.fields.push_back({std::move(fieldName), std::move(fieldType)});
        }
        abi.structs.push_back(std::move(entry));
    }
    DK_TRY(numActions, d.readVaruint32());
    for (uint32_t i = 0; i < numActions; i++) {
        DK_TRY(name, d.readInt<uint64_t>());
        DK_TRY(type, d.readString());
        DK_TRY(ricardian, d.readString());
        abi.actions.push_back({Name(name), std::move(type), std::move(ricardian)});
    }
    DK_TRY(numTables, d.readVaruint32());
    for (uint32_t i = 0; i < numTables; i++) {
        DK_TRY(name, d.readInt<uint64_t>());
        DK_TRY(indexType, d.readString());
        Table table{Name(name), std::move(indexType), {}, {}, ""};
        DK_TRY(numKeyNames, d.readVaruint32());
        for (uint32_t j = 0; j < numKeyNames; j++) {
            DK_TRY(key, d.readString());
            table.key_names.push_back(std::move(key));
        }
        DK_TRY(numKeyTypes, d.readVaruint32());
        for (uint32_t j = 0; j < numKeyTypes; j++) {
            DK_TRY(key, d.readString());
            table.key_types.push_back(std::move(key));
        }
        DK_TRY(type, d.readString());
        table.type = std::move(type);
        abi.tables.push_back(std::move(table));
    }
    DK_TRY(numClauses, d.readVaruint32());
    for (uint32_t i = 0; i < numClauses; i++) {
        DK_TRY(id, d.readString());
        DK_TRY(body, d.readString());
        abi.ricardian_clauses.push_back({std::move(id), std::move(body)});
    }
    // error_messages, never used?
    DK_TRY(numErrors, d.readVaruint32());
    for (uint32_t i = 0; i < numErrors; i++) {
        DK_CHECK(d.advance(8));  // uint64 error_code
        DK_TRY(msgLen, d.readVaruint32());
        DK_CHECK(d.advance(msgLen));  // string error_msgr
    }
    // extensions, not used
    DK_TRY(numExtensions, d.readVaruint32());
    for (uint32_t i = 0; i < numExtensions; i++) {
        DK_CHECK(d.advance(2));  // uint16 type
        DK_TRY(dataLen, d.readVaruint32());
        DK_CHECK(d.advance(dataLen));  // bytes data
    }
    // variants is a binary extension for some reason even though extensions are
    // defined on the type
    if (d.canRead()) {
        DK_TRY(numVariants, d.readVaruint32());
        for (uint32_t i = 0; i < numVariants; i++) {
            DK_TRY(name, d.readString());
            Variant variant{std::move(name), {}};
            DK_TRY(numVariantTypes, d.readVaruint32());
            for (uint32_t j = 0; j < numVariantTypes; j++) {
                DK_TRY(type, d.readString());
                variant.types.push_back(std::move(type));
            }
            abi.variants.push_back(std::move(variant));
        }
    }
    if (d.canRead()) {
        DK_TRY(numActionResults, d.readVaruint32());
        for (uint32_t i = 0; i < numActionResults; i++) {
            DK_TRY(name, d.readInt<uint64_t>());
            DK_TRY(resultType, d.readString());
            abi.action_results.push_back({Name(name), std::move(resultType)});
        }
    }
    return abi;
}

Result<void> ABI::toABI(ABIEncoder& e) const {
    e.writeString(version);
    e.writeVaruint32(static_cast<uint32_t>(types.size()));
    for (const auto& type : types) {
        e.writeString(type.new_type_name);
        e.writeString(type.type);
    }
    e.writeVaruint32(static_cast<uint32_t>(structs.size()));
    for (const auto& entry : structs) {
        e.writeString(entry.name);
        e.writeString(entry.base);
        e.writeVaruint32(static_cast<uint32_t>(entry.fields.size()));
        for (const auto& field : entry.fields) {
            e.writeString(field.name);
            e.writeString(field.type);
        }
    }
    e.writeVaruint32(static_cast<uint32_t>(actions.size()));
    for (const auto& action : actions) {
        e.writeInt<uint64_t>(action.name.value);
        e.writeString(action.type);
        e.writeString(action.ricardian_contract);
    }
    e.writeVaruint32(static_cast<uint32_t>(tables.size()));
    for (const auto& table : tables) {
        e.writeInt<uint64_t>(table.name.value);
        e.writeString(table.index_type);
        e.writeVaruint32(static_cast<uint32_t>(table.key_names.size()));
        for (const auto& key : table.key_names) e.writeString(key);
        e.writeVaruint32(static_cast<uint32_t>(table.key_types.size()));
        for (const auto& key : table.key_types) e.writeString(key);
        e.writeString(table.type);
    }
    e.writeVaruint32(static_cast<uint32_t>(ricardian_clauses.size()));
    for (const auto& clause : ricardian_clauses) {
        e.writeString(clause.id);
        e.writeString(clause.body);
    }
    e.writeVaruint32(0);  // error_messages
    e.writeVaruint32(0);  // extensions
    e.writeVaruint32(static_cast<uint32_t>(variants.size()));
    for (const auto& variant : variants) {
        e.writeString(variant.name);
        e.writeVaruint32(static_cast<uint32_t>(variant.types.size()));
        for (const auto& type : variant.types) e.writeString(type);
    }
    e.writeVaruint32(static_cast<uint32_t>(action_results.size()));
    for (const auto& result : action_results) {
        e.writeInt<uint64_t>(result.name.value);
        e.writeString(result.result_type);
    }
    return {};
}

std::string ABI::ResolvedNode::typeName() const {
    std::string rv = name;
    if (isArray) {
        if (size) {
            rv += "[" + std::to_string(*size) + "]";
        } else {
            rv += "[]";
        }
    }
    if (isOptional) rv += "?";
    if (isExtension) rv += "$";
    return rv;
}

std::optional<std::vector<std::pair<std::string, const ABI::ResolvedNode*>>>
ABI::ResolvedNode::allFields() const {
    const ResolvedNode* current = this;
    std::vector<std::pair<std::string, const ResolvedNode*>> rv;
    std::set<std::string> seen;
    do {
        if (!current->fields) {
            return std::nullopt;  // invalid struct
        }
        if (seen.contains(current->name)) {
            return std::nullopt;  // circular ref
        }
        rv.insert(rv.begin(), current->fields->begin(), current->fields->end());
        seen.insert(current->name);
        current = current->base;
    } while (current != nullptr);
    return rv;
}

ABI::ResolvedNode ABI::ResolvedNode::parse(std::string fullName, int id) {
    ResolvedNode node;
    std::string name = std::move(fullName);
    if (name.ends_with('$')) {
        name.pop_back();
        node.isExtension = true;
    }
    if (name.ends_with('?')) {
        name.pop_back();
        node.isOptional = true;
    }
    if (name.ends_with("[]")) {
        name.resize(name.size() - 2);
        node.isArray = true;
    }
    // fixed size array: name[N]
    if (name.ends_with(']')) {
        const size_t open = name.rfind('[');
        if (open != std::string::npos && open + 1 < name.size() - 1) {
            bool digits = true;
            for (size_t i = open + 1; i < name.size() - 1; i++) {
                if (name[i] < '0' || name[i] > '9') {
                    digits = false;
                    break;
                }
            }
            if (digits) {
                node.size = static_cast<uint32_t>(
                    std::stoul(name.substr(open + 1, name.size() - open - 2)));
                node.isArray = true;
                name.resize(open);
            }
        }
    }
    node.id = id;
    node.name = std::move(name);
    return node;
}

struct ABI::ResolveContext {
    std::vector<std::unique_ptr<ResolvedNode>>* arena;
    std::map<std::string, const ResolvedNode*> types;
    int id = 0;
};

const ABI::ResolvedNode* ABI::resolve(const std::string& name, ResolveContext& ctx) const {
    if (const auto existing = ctx.types.find(name); existing != ctx.types.end()) {
        return existing->second;
    }
    auto owned = std::make_unique<ResolvedNode>(ResolvedNode::parse(name, ++ctx.id));
    ResolvedNode* type = owned.get();
    ctx.arena->push_back(std::move(owned));
    ctx.types[type->typeName()] = type;
    for (const auto& typeDef : types) {
        if (typeDef.new_type_name == type->name) {
            type->ref = resolve(typeDef.type, ctx);
            return type;
        }
    }
    if (const Struct* entry = getStruct(type->name)) {
        if (!entry->base.empty()) {
            type->base = resolve(entry->base, ctx);
        }
        type->fields.emplace();
        for (const auto& field : entry->fields) {
            type->fields->emplace_back(field.name, resolve(field.type, ctx));
        }
        return type;
    }
    if (const Variant* variant = getVariant(type->name)) {
        type->variant.emplace();
        for (const auto& variantType : variant->types) {
            type->variant->push_back(resolve(variantType, ctx));
        }
        return type;
    }
    // builtin or unknown type
    return type;
}

ABI::ResolvedType ABI::resolveType(const std::string& name) const {
    auto arena = std::make_shared<std::vector<std::unique_ptr<ResolvedNode>>>();
    ResolveContext ctx{arena.get(), {}, 0};
    const ResolvedNode* root = resolve(name, ctx);
    return ResolvedType(std::move(arena), root);
}

ABI::ResolvedAll ABI::resolveAll() const {
    auto arena = std::make_shared<std::vector<std::unique_ptr<ResolvedNode>>>();
    ResolveContext ctx{arena.get(), {}, 0};
    ResolvedAll rv;
    for (const auto& t : types) {
        rv.types.emplace_back(arena, resolve(t.new_type_name, ctx));
    }
    for (const auto& v : variants) {
        rv.variants.emplace_back(arena, resolve(v.name, ctx));
    }
    for (const auto& s : structs) {
        rv.structs.emplace_back(arena, resolve(s.name, ctx));
    }
    return rv;
}

const ABI::Struct* ABI::getStruct(std::string_view name) const {
    for (const auto& entry : structs) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

const ABI::Variant* ABI::getVariant(std::string_view name) const {
    for (const auto& variant : variants) {
        if (variant.name == name) return &variant;
    }
    return nullptr;
}

std::optional<std::string> ABI::getActionType(const Name& actionName) const {
    for (const auto& action : actions) {
        if (action.name == actionName) return action.type;
    }
    return std::nullopt;
}

bool ABI::equals(const ABI& other) const {
    if (version != other.version || types.size() != other.types.size() ||
        structs.size() != other.structs.size() || actions.size() != other.actions.size() ||
        tables.size() != other.tables.size() ||
        ricardian_clauses.size() != other.ricardian_clauses.size() ||
        variants.size() != other.variants.size() ||
        action_results.size() != other.action_results.size()) {
        return false;
    }
    ABIEncoder a, b;
    if (!toABI(a) || !other.toABI(b)) return false;
    return a.getBytes() == b.getBytes();
}

json ABI::toJSON() const {
    json rv = json::object();
    rv["version"] = version;
    rv["types"] = json::array();
    for (const auto& t : types) {
        rv["types"].push_back({{"new_type_name", t.new_type_name}, {"type", t.type}});
    }
    rv["structs"] = json::array();
    for (const auto& s : structs) {
        json fields = json::array();
        for (const auto& f : s.fields) {
            fields.push_back({{"name", f.name}, {"type", f.type}});
        }
        rv["structs"].push_back({{"name", s.name}, {"base", s.base}, {"fields", fields}});
    }
    rv["actions"] = json::array();
    for (const auto& a : actions) {
        rv["actions"].push_back({{"name", a.name.toString()},
                                 {"type", a.type},
                                 {"ricardian_contract", a.ricardian_contract}});
    }
    rv["tables"] = json::array();
    for (const auto& t : tables) {
        rv["tables"].push_back({{"name", t.name.toString()},
                                {"index_type", t.index_type},
                                {"key_names", t.key_names},
                                {"key_types", t.key_types},
                                {"type", t.type}});
    }
    rv["ricardian_clauses"] = json::array();
    for (const auto& c : ricardian_clauses) {
        rv["ricardian_clauses"].push_back({{"id", c.id}, {"body", c.body}});
    }
    rv["error_messages"] = json::array();
    rv["abi_extensions"] = json::array();
    rv["variants"] = json::array();
    for (const auto& v : variants) {
        rv["variants"].push_back({{"name", v.name}, {"types", v.types}});
    }
    rv["action_results"] = json::array();
    for (const auto& r : action_results) {
        rv["action_results"].push_back({{"name", r.name.toString()}, {"result_type", r.result_type}});
    }
    return rv;
}

}  // namespace dwarfkit
