// Port of antelope src/chain/permission-level.ts
#pragma once

#include <dwarfkit/antelope/chain/name.hpp>
#include <dwarfkit/antelope/serializer/serializable.hpp>

namespace dwarfkit {

// Antelope/EOSIO Permission Level, a.k.a "auth".
struct PermissionLevel {
    DK_STRUCT("permission_level")
    Name actor;
    Name permission;
    DK_FIELDS(actor, permission)

    // Can be expressed as a string in the format <actor>@<permission>.
    static Result<PermissionLevel> from(std::string_view value);
    static constexpr PermissionLevel from(Name actor, Name permission) {
        return {actor, permission};
    }

    bool equals(const PermissionLevel& other) const {
        return actor == other.actor && permission == other.permission;
    }

    // Compare with another permission level by actor, then permission.
    constexpr int compare(const PermissionLevel& other) const {
        const int byActor = actor.compare(other.actor);
        return byActor != 0 ? byActor : permission.compare(other.permission);
    }

    std::string toString() const { return actor.toString() + "@" + permission.toString(); }
};

}  // namespace dwarfkit
