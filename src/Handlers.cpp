#include "silica/Handlers.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace silica {
namespace {

struct HandlerRegistry {
    std::mutex                                             mutex;
    std::unordered_map<std::string, SilicaHandlers::ActionHandler> actions;
    std::unordered_map<std::string, SilicaHandlers::ValueHandler>  values;
};

HandlerRegistry& registry() {
    static HandlerRegistry instance;
    return instance;
}

} // namespace

void SilicaHandlers::Register(std::string name, ActionHandler handler) {
    if (name.empty() || !handler) return;
    std::lock_guard lock(registry().mutex);
    registry().actions[std::move(name)] = std::move(handler);
}

void SilicaHandlers::Register(std::string name, ValueHandler handler) {
    if (name.empty() || !handler) return;
    std::lock_guard lock(registry().mutex);
    registry().values[std::move(name)] = std::move(handler);
}

bool SilicaHandlers::Unregister(std::string_view name) {
    std::lock_guard lock(registry().mutex);
    bool const      action = registry().actions.erase(std::string(name)) > 0;
    bool const      value  = registry().values.erase(std::string(name)) > 0;
    return action || value;
}

bool SilicaHandlers::Invoke(std::string_view name) {
    ActionHandler action;
    ValueHandler  value;
    {
        std::lock_guard lock(registry().mutex);
        std::string const key(name);
        if (auto const found = registry().actions.find(key); found != registry().actions.end()) {
            action = found->second;
        } else if (auto const foundValue = registry().values.find(key);
                   foundValue != registry().values.end()) {
            value = foundValue->second;
        }
    }
    if (!action && !value) return false;
    // Call outside the lock: a handler may register or unregister others.
    try {
        if (action) action();
        else value(std::string_view{});
    } catch (...) {
        return false;
    }
    return true;
}

bool SilicaHandlers::Invoke(std::string_view name, std::string_view value) {
    ValueHandler handler;
    {
        std::lock_guard lock(registry().mutex);
        auto const      found = registry().values.find(std::string(name));
        if (found == registry().values.end()) return false;
        handler = found->second;
    }
    try {
        handler(value);
    } catch (...) {
        return false;
    }
    return true;
}

bool SilicaHandlers::Has(std::string_view name) {
    std::lock_guard lock(registry().mutex);
    std::string const key(name);
    return registry().actions.contains(key) || registry().values.contains(key);
}

void SilicaHandlers::Clear() {
    std::lock_guard lock(registry().mutex);
    registry().actions.clear();
    registry().values.clear();
}

} // namespace silica
