#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace silica {

// Name-based callbacks keep .sil decoupled from C++: the document only stores
// "onChange: handle_volume", the mod registers the lambda at any time.
//
// Plain handlers are for actions without data (onClick). Value handlers receive
// the widget's new value as text ("true", "42.5", the edited string, or the
// selected index), so one registry serves every form control.
class SilicaHandlers {
public:
    using ActionHandler = std::function<void()>;
    using ValueHandler  = std::function<void(std::string_view)>;

    static void Register(std::string name, ActionHandler handler);
    static void Register(std::string name, ValueHandler handler);
    static bool Unregister(std::string_view name);

    // Action dispatch; falls back to a value handler invoked with an empty
    // value so a name can be used for both.
    static bool Invoke(std::string_view name);
    static bool Invoke(std::string_view name, std::string_view value);

    static bool Has(std::string_view name);
    static void Clear();
};

} // namespace silica
