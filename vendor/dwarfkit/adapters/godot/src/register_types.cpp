#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "dk_session_kit.h"
#include "dk_user_interface.h"

using namespace godot;

namespace {

void initialize_dwarfkit(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    ClassDB::register_class<dwarfkit_godot::DkUserInterface>();
    ClassDB::register_class<dwarfkit_godot::DkSession>();
    ClassDB::register_class<dwarfkit_godot::DkSessionKit>();
}

void uninitialize_dwarfkit(ModuleInitializationLevel level) {
    (void)level;
}

}  // namespace

extern "C" {
GDExtensionBool GDE_EXPORT dwarfkit_library_init(GDExtensionInterfaceGetProcAddress get_proc,
                                                 const GDExtensionClassLibraryPtr library,
                                                 GDExtensionInitialization* initialization) {
    GDExtensionBinding::InitObject init(get_proc, library, initialization);
    init.register_initializer(initialize_dwarfkit);
    init.register_terminator(uninitialize_dwarfkit);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
}
