// Umbrella for the dwarfkit/resources module (@wharfkit/resources).
#pragma once

#include <dwarfkit/resources/powerup.hpp>
#include <dwarfkit/resources/ram.hpp>
#include <dwarfkit/resources/resources.hpp>
#include <dwarfkit/resources/rex.hpp>

namespace dwarfkit {

struct Resources::V1View {
    PowerUpAPI powerup;
    RAMAPI ram;
    REXAPI rex;
};

inline Resources::V1View Resources::v1() const {
    return V1View{PowerUpAPI(*this), RAMAPI(*this), REXAPI(*this)};
}

}  // namespace dwarfkit
