#pragma once

#include "Widget.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace silica {

// Type-name to constructor map used by the .sil compiler. Nothing in the core
// lists the concrete widgets: Container/Label/Button/Panel register themselves
// through RegisterBuiltins, and a mod can add Slider, ScrollView, ... the same
// way at any time.
class WidgetFactory {
public:
    using Creator = std::function<WidgetPtr()>;

    static void                     Register(std::string type, Creator creator);
    static bool                     Unregister(std::string_view type);
    static WidgetPtr                Create(std::string_view type);
    static bool                     Has(std::string_view type);
    static std::vector<std::string> Types();
    static void                     RegisterBuiltins();
};

} // namespace silica
