// Port of antelope src/chain/abi.ts
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dwarfkit/antelope/chain/blob.hpp>
#include <dwarfkit/antelope/chain/name.hpp>
#include <dwarfkit/antelope/serializer/decoder.hpp>
#include <dwarfkit/antelope/serializer/encoder.hpp>
#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

class ABI {
public:
    static constexpr std::string_view abiName = "abi";
    static constexpr std::string_view defaultVersion = "eosio::abi/1.1";

    struct TypeDef {
        std::string new_type_name;
        std::string type;
    };
    struct Field {
        std::string name;
        std::string type;
    };
    struct Struct {
        std::string name;
        std::string base;
        std::vector<Field> fields;
    };
    struct Action {
        Name name;
        std::string type;
        std::string ricardian_contract;
    };
    struct Table {
        Name name;
        std::string index_type;
        std::vector<std::string> key_names;
        std::vector<std::string> key_types;
        std::string type;
    };
    struct Clause {
        std::string id;
        std::string body;
    };
    struct Variant {
        std::string name;
        std::vector<std::string> types;
    };
    struct ActionResult {
        Name name;
        std::string result_type;
    };

    std::string version = std::string(defaultVersion);
    std::vector<TypeDef> types;
    std::vector<Variant> variants;
    std::vector<Struct> structs;
    std::vector<Action> actions;
    std::vector<Table> tables;
    std::vector<Clause> ricardian_clauses;
    std::vector<ActionResult> action_results;

    // Node in the resolved type graph. May be cyclic; owned by the arena the
    // ResolvedType handle keeps alive.
    struct ResolvedNode {
        std::string name;
        int id = 0;
        bool isArray = false;
        bool isOptional = false;
        bool isExtension = false;
        std::optional<uint32_t> size;  // fixed size array

        const ResolvedNode* base = nullptr;
        std::optional<std::vector<std::pair<std::string, const ResolvedNode*>>> fields;
        std::optional<std::vector<const ResolvedNode*>> variant;
        const ResolvedNode* ref = nullptr;

        // Type name including suffixes: [] array, ? optional, $ binary ext
        std::string typeName() const;

        // All fields including base struct(s); nullopt if not a (valid) struct.
        std::optional<std::vector<std::pair<std::string, const ResolvedNode*>>> allFields() const;

        // Parse the suffixes off a full type name (the TS ResolvedType ctor).
        static ResolvedNode parse(std::string fullName, int id = 0);
    };

    // Owning handle over a resolved type graph.
    class ResolvedType {
    public:
        ResolvedType() = default;
        ResolvedType(std::shared_ptr<const std::vector<std::unique_ptr<ResolvedNode>>> arena,
                     const ResolvedNode* node)
            : arena_(std::move(arena)), node_(node) {}

        const ResolvedNode& operator*() const { return *node_; }
        const ResolvedNode* operator->() const { return node_; }
        const ResolvedNode* get() const { return node_; }
        explicit operator bool() const { return node_ != nullptr; }

    private:
        std::shared_ptr<const std::vector<std::unique_ptr<ResolvedNode>>> arena_;
        const ResolvedNode* node_ = nullptr;
    };

    struct ResolvedAll {
        std::vector<ResolvedType> types;
        std::vector<ResolvedType> variants;
        std::vector<ResolvedType> structs;
    };

    static Result<ABI> from(const json& value);
    static Result<ABI> from(std::string_view jsonText);
    static Result<ABI> from(const Blob& value);
    static ABI from(const ABI& value) { return value; }

    static Result<ABI> fromABI(ABIDecoder& decoder);
    Result<void> toABI(ABIEncoder& encoder) const;

    ResolvedType resolveType(const std::string& name) const;
    ResolvedAll resolveAll() const;

    const Struct* getStruct(std::string_view name) const;
    const Variant* getVariant(std::string_view name) const;

    // Return arguments type of an action in this ABI.
    std::optional<std::string> getActionType(const Name& actionName) const;
    std::optional<std::string> getActionType(std::string_view actionName) const {
        return getActionType(Name::from(actionName));
    }

    bool equals(const ABI& other) const;

    json toJSON() const;

private:
    struct ResolveContext;
    const ResolvedNode* resolve(const std::string& name, ResolveContext& ctx) const;
};

// string | Partial<ABI.Def> | ABI | Blob
using ABIDef = ABI;

template <class T, class Enable>
struct abi_traits;

template <>
struct abi_traits<ABI, void> {
    static constexpr std::string_view abiName = "abi";
    static Result<void> toABI(const ABI& v, ABIEncoder& e) { return v.toABI(e); }
    static Result<ABI> fromABI(ABIDecoder& d) { return ABI::fromABI(d); }
    static json toJSON(const ABI& v) { return v.toJSON(); }
    static Result<ABI> fromJSON(const json& j) { return ABI::from(j); }
    static ABI abiDefault() { return {}; }
};

}  // namespace dwarfkit
