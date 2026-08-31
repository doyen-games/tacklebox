#include "generator.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <dwarfkit/contract/utils.hpp>

namespace dwarfkit::dkgen {

namespace {

// ABI builtin -> dwarfkit type. Mirrors the cli's ANTELOPE_CLASSES lookup
// against the types the dwarfkit serializer ships.
const std::map<std::string, std::string>& builtinTypes() {
    static const std::map<std::string, std::string> types = {
        {"bool", "bool"},
        {"string", "std::string"},
        {"uint8", "uint8_t"},
        {"uint16", "uint16_t"},
        {"uint32", "uint32_t"},
        {"uint64", "uint64_t"},
        {"int8", "int8_t"},
        {"int16", "int16_t"},
        {"int32", "int32_t"},
        {"int64", "int64_t"},
        {"uint128", "UInt128"},
        {"int128", "Int128"},
        {"varuint32", "VarUInt"},
        {"varint32", "VarInt"},
        {"float32", "float"},
        {"float64", "double"},
        {"float128", "Float128"},
        {"name", "Name"},
        {"asset", "Asset"},
        {"symbol", "Asset::Symbol"},
        {"symbol_code", "Asset::SymbolCode"},
        {"extended_asset", "ExtendedAsset"},
        {"extended_symbol", "ExtendedSymbol"},
        {"bytes", "Bytes"},
        {"blob", "Blob"},
        {"checksum160", "Checksum160"},
        {"checksum256", "Checksum256"},
        {"checksum512", "Checksum512"},
        {"block_id_type", "BlockId"},
        {"public_key", "PublicKey"},
        {"signature", "Signature"},
        {"time_point", "TimePoint"},
        {"time_point_sec", "TimePointSec"},
        {"block_timestamp_type", "BlockTimestamp"},
        {"key_weight", "KeyWeight"},
        {"permission_level_weight", "PermissionLevelWeight"},
        {"wait_weight", "WaitWeight"},
        {"weight_type", "Weight"},
        {"authority", "Authority"},
        {"permission_level", "PermissionLevel"},
        {"action", "Action"},
        {"transaction_extension", "TransactionExtension"},
        {"transaction_header", "TransactionHeader"},
        {"transaction", "Transaction"},
        {"abi", "ABI"},
    };
    return types;
}

const std::set<std::string>& cppKeywords() {
    static const std::set<std::string> keywords = {
        "alignas",   "alignof",  "and",     "asm",      "auto",     "bool",     "break",
        "case",      "catch",    "char",    "class",    "concept",  "const",    "constexpr",
        "continue",  "default",  "delete",  "do",       "double",   "else",     "enum",
        "explicit",  "export",   "extern",  "false",    "float",    "for",      "friend",
        "goto",      "if",       "inline",  "int",      "long",     "mutable",  "namespace",
        "new",       "noexcept", "not",     "nullptr",  "operator", "or",       "private",
        "protected", "public",   "register", "requires", "return",  "short",    "signed",
        "sizeof",    "static",   "struct",  "switch",   "template", "this",     "throw",
        "true",      "try",      "typedef", "typeid",   "typename", "union",    "unsigned",
        "using",     "virtual",  "void",    "volatile", "while",    "xor",
    };
    return keywords;
}

bool isArithmetic(const std::string& cppType) {
    static const std::set<std::string> arithmetic = {
        "bool",   "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t",
        "int16_t", "int32_t", "int64_t",  "float",    "double",
    };
    return arithmetic.count(cppType) > 0;
}

// formatClassName: dots removed (the cli keeps the original casing)
std::string className(const std::string& name) {
    std::string rv;
    for (const char c : name) {
        if (c != '.') {
            rv += c;
        }
    }
    return rv;
}

struct ParsedType {
    std::string base;
    bool array = false;
    bool optional = false;
    bool extension = false;
};

// extractDecorator + the $ handling from parseType: strip one suffix layer at
// a time (the cli strips $ first, then one of ? or []).
ParsedType parseTypeString(std::string type) {
    ParsedType rv;
    std::erase_if(type, [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
    if (type.ends_with("$")) {
        rv.extension = true;
        type.pop_back();
    }
    if (type.ends_with("?")) {
        rv.optional = true;
        type.pop_back();
    }
    if (type.ends_with("[]")) {
        rv.array = true;
        type.resize(type.size() - 2);
    }
    rv.base = type;
    return rv;
}

struct StructData {
    std::string name;
    std::string base;
    std::vector<ABI::Field> fields;
    bool variant = false;
    std::vector<std::string> variantTypes;
};

class Generator {
public:
    Generator(std::string contractName, const ABI& abi, const GenerateOptions& options)
        : contractName_(std::move(contractName)), abi_(abi), options_(options) {}

    Result<std::string> run();

private:
    bool isAbiStructOrVariant(const std::string& name) const {
        return std::any_of(abi_.structs.begin(), abi_.structs.end(),
                           [&](const auto& s) { return s.name == name; }) ||
               std::any_of(abi_.variants.begin(), abi_.variants.end(),
                           [&](const auto& v) { return v.name == name; });
    }

    // findTypeFromAlias: one level of abi.types resolution
    std::optional<std::string> aliasTarget(const std::string& name) const {
        for (const auto& alias : abi_.types) {
            if (alias.new_type_name == name) {
                return alias.type;
            }
        }
        return std::nullopt;
    }

    // The dependency name a field type resolves to (through one alias level),
    // or empty when it is not a generated struct/variant.
    std::string dependencyName(const std::string& fieldType) const {
        ParsedType parsed = parseTypeString(fieldType);
        if (const auto alias = aliasTarget(parsed.base)) {
            const ParsedType aliasParsed = parseTypeString(*alias);
            parsed.base = aliasParsed.base;
        }
        return isAbiStructOrVariant(parsed.base) ? parsed.base : std::string();
    }

    // findInternalType: the C++ type for a field type string.
    Result<std::string> cppType(const std::string& fieldType) const {
        ParsedType parsed = parseTypeString(fieldType);
        if (const auto alias = aliasTarget(parsed.base)) {
            const ParsedType aliasParsed = parseTypeString(*alias);
            parsed.base = aliasParsed.base;
            parsed.array = parsed.array || aliasParsed.array;
            parsed.optional = parsed.optional || aliasParsed.optional;
        }
        std::string type;
        if (isAbiStructOrVariant(parsed.base)) {
            type = "Types::" + className(parsed.base);
        } else if (builtinTypes().count(parsed.base)) {
            type = builtinTypes().at(parsed.base);
        } else {
            return err(ErrorKind::Invalid, "Unknown ABI type: " + parsed.base);
        }
        if (parsed.array) {
            type = "std::vector<" + type + ">";
        }
        if (parsed.optional && !parsed.extension) {
            type = "std::optional<" + type + ">";
        }
        if (parsed.extension) {
            type = "BinaryExtension<" + type + ">";
        }
        return type;
    }

    Result<std::vector<StructData>> collectStructs() const {
        std::vector<StructData> rv;
        for (const auto& variant : abi_.variants) {
            StructData data;
            data.name = variant.name;
            data.variant = true;
            data.variantTypes = variant.types;
            rv.push_back(data);
        }
        for (const auto& abiStruct : abi_.structs) {
            StructData data;
            data.name = abiStruct.name;
            data.base = abiStruct.base;
            data.fields = abiStruct.fields;
            rv.push_back(data);
        }
        return rv;
    }

    // orderStructs/findDependencies: dependencies first, first occurrence wins
    void appendWithDependencies(const StructData& item, const std::vector<StructData>& all,
                                std::vector<const StructData*>& ordered,
                                std::set<std::string>& seen,
                                std::set<std::string>& visiting) const {
        if (seen.count(item.name) || visiting.count(item.name)) {
            return;
        }
        visiting.insert(item.name);
        const auto findByName = [&](const std::string& name) -> const StructData* {
            for (const auto& candidate : all) {
                if (candidate.name == name) {
                    return &candidate;
                }
            }
            return nullptr;
        };
        if (!item.base.empty()) {
            if (const auto* base = findByName(item.base)) {
                appendWithDependencies(*base, all, ordered, seen, visiting);
            }
        }
        for (const auto& type : item.variantTypes) {
            const std::string dep = dependencyName(type);
            if (!dep.empty() && dep != item.name) {
                if (const auto* found = findByName(dep)) {
                    appendWithDependencies(*found, all, ordered, seen, visiting);
                }
            }
        }
        for (const auto& field : item.fields) {
            const std::string dep = dependencyName(field.type);
            if (!dep.empty() && dep != item.name) {
                if (const auto* found = findByName(dep)) {
                    appendWithDependencies(*found, all, ordered, seen, visiting);
                }
            }
        }
        visiting.erase(item.name);
        seen.insert(item.name);
        ordered.push_back(findByName(item.name));
    }

    std::string contractName_;
    const ABI& abi_;
    GenerateOptions options_;
};

std::string blobLiteral(const std::string& base64) {
    std::string rv;
    for (size_t i = 0; i < base64.size(); i += 96) {
        rv += "    \"" + base64.substr(i, 96) + "\"";
        if (i + 96 < base64.size()) {
            rv += "\n";
        }
    }
    return rv;
}

Result<std::string> Generator::run() {
    const std::string ns =
        options_.namespaceName.empty() ? sanitizeIdentifier(contractName_)
                                       : options_.namespaceName;

    DK_TRY(structs, collectStructs());
    std::vector<const StructData*> ordered;
    {
        std::set<std::string> seen;
        std::set<std::string> visiting;
        for (const auto& item : structs) {
            appendWithDependencies(item, structs, ordered, seen, visiting);
        }
    }

    std::string out;
    out += "// Generated by dkgen from the " + contractName_ + " ABI. Do not edit.\n";
    out += "#pragma once\n\n";
    out += "#include <dwarfkit/contract.hpp>\n\n";
    out += "namespace dwarfkit::gen::" + ns + " {\n\n";

    // abiBlob / abi()
    const Blob blob = abiToBlob(abi_);
    out += "inline const char* const abiBlob =\n" + blobLiteral(blob.toString()) + ";\n\n";
    out += "inline const ABI& abi() {\n";
    out += "    static const ABI decoded = [] {\n";
    out += "        const auto parsed = Blob::from(abiBlob);\n";
    out += "        if (parsed) {\n";
    out += "            const auto value = Serializer::decode<ABI>(parsed->array);\n";
    out += "            if (value) {\n";
    out += "                return *value;\n";
    out += "            }\n";
    out += "        }\n";
    out += "        return ABI();\n";
    out += "    }();\n";
    out += "    return decoded;\n";
    out += "}\n\n";

    // Types namespace
    out += "namespace Types {\n\n";
    for (const auto* item : ordered) {
        if (item->variant) {
            std::string types;
            for (const auto& type : item->variantTypes) {
                DK_TRY(cpp, cppType(type));
                if (!types.empty()) {
                    types += ", ";
                }
                types += cpp;
            }
            out += "DK_VARIANT(" + className(item->name) + ", \"" + item->name + "\", " +
                   types + ")\n\n";
            continue;
        }
        out += "struct " + className(item->name) + " ";
        if (!item->base.empty()) {
            out += ": " + className(item->base) + " ";
        }
        out += "{\n";
        if (item->base.empty()) {
            out += "    DK_STRUCT(\"" + item->name + "\")\n";
        } else {
            out += "    DK_STRUCT_BASE(\"" + item->name + "\", " + className(item->base) +
                   ")\n";
        }
        std::string fieldNames;
        for (const auto& field : item->fields) {
            if (cppKeywords().count(field.name)) {
                return err(ErrorKind::Unsupported,
                           "ABI field name '" + field.name + "' in struct '" + item->name +
                               "' is a C++ keyword; DK_FIELDS derives the wire name from the "
                               "member name, so it cannot be renamed");
            }
            DK_TRY(cpp, cppType(field.type));
            out += "    " + cpp + " " + field.name;
            if (isArithmetic(cpp)) {
                out += cpp == "bool" ? " = false" : " = 0";
            }
            out += ";\n";
            if (!fieldNames.empty()) {
                fieldNames += ", ";
            }
            fieldNames += field.name;
        }
        if (!fieldNames.empty()) {
            out += "    DK_FIELDS(" + fieldNames + ")\n";
        } else {
            out += "    DK_NO_FIELDS\n";
        }
        out += "};\n\n";
    }
    out += "}  // namespace Types\n\n";

    // Contract subclass
    out += "class Contract : public dwarfkit::Contract {\n";
    out += "public:\n";
    out += "    explicit Contract(const std::shared_ptr<APIClient>& client,\n";
    out += "                      const Name& account = Name::from(\"" + contractName_ +
           "\"))\n";
    // qualified: the base class's abi member shadows the namespace function
    out += "        : dwarfkit::Contract({.abi = " + ns +
           "::abi(), .account = account, .client = client}) {}\n";
    for (const auto& action : abi_.actions) {
        const std::string method = sanitizeIdentifier(action.name.toString());
        const std::string paramType = "Types::" + className(action.type);
        if (!isAbiStructOrVariant(action.type)) {
            continue;
        }
        out += "\n";
        out += "    Result<Action> " + method + "(const " + paramType +
               "& value, const ActionOptions& options = {}) const {\n";
        out += "        DK_TRY(data, Serializer::encode(value));\n";
        out += "        return action(Name::from(\"" + action.name.toString() +
               "\"), json(data.hexString()), options);\n";
        out += "    }\n";
    }
    for (const auto& table : abi_.tables) {
        const std::string method = sanitizeIdentifier(table.name.toString()) + "Table";
        out += "\n";
        out += "    Result<dwarfkit::Table> " + method +
               "(const json& scope = {}) const {\n";
        out += "        return table(Name::from(\"" + table.name.toString() +
               "\"), scope);\n";
        out += "    }\n";
    }
    out += "};\n\n";
    out += "}  // namespace dwarfkit::gen::" + ns + "\n";
    return out;
}

}  // namespace

std::string sanitizeIdentifier(const std::string& name) {
    std::string rv;
    for (const char c : name) {
        rv += c == '.' ? '_' : c;
    }
    if (!rv.empty() && std::isdigit(static_cast<unsigned char>(rv[0]))) {
        rv = "_" + rv;
    }
    if (cppKeywords().count(rv)) {
        rv += "_";
    }
    return rv;
}

Result<std::string> generateContractHeader(const std::string& contractName, const ABI& abi,
                                           const GenerateOptions& options) {
    return Generator(contractName, abi, options).run();
}

}  // namespace dwarfkit::dkgen
