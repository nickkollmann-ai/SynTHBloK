#pragma once
// "Make module pictures" in SynTHBloK's right-click menus: Rack takes a picture of every installed module for
// PATCH GEN's patch map (liquidnick.com/patchgen), without the user running a command. See ModulePictures.cpp.
#include <rack.hpp>

namespace ModulePictures {
// adds a separator, then the item (or the progress / done line) to a module's context menu
void appendMenu(rack::ui::Menu* menu);
}
