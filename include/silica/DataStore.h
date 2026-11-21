#pragma once

#include "Variant.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace silica {

// Receives a notification whenever a watched key changes value. Widgets
// implement it to re-evaluate the bindings that read that key.
class DataObserver {
public:
    virtual ~DataObserver() = default;
    virtual void OnDataChanged(std::string_view key) = 0;
};

// Process-wide reactive state. The model layer writes through Set; every
// widget that bound an expression to a changed key gets OnDataChanged(key)
// immediately, synchronously, on the writing thread.
//
// Notifications are delivered outside the store's lock, so a handler may read
// or write the store again (a click script assigning several keys is a normal
// case). Observers are held weakly: dropping a widget tree drops its
// subscriptions with it.
class DataStore {
public:
    // Stores `value` and notifies observers when the value actually changed.
    static void Set(std::string key, Variant value);

    // Null when the key was never set. Callers that need to distinguish "unset"
    // from an empty value use Has.
    static Variant Get(std::string_view key);

    static bool Has(std::string_view key);

    // Removes the key and notifies observers as if it became null.
    static bool Remove(std::string_view key);

    // Drops every key. No notifications: meant for tests and teardown.
    static void Clear();

    [[nodiscard]] static std::size_t Size();

    static void Subscribe(std::string_view key, std::shared_ptr<DataObserver> observer);
    static void Unsubscribe(std::string_view key, DataObserver const* observer);
};

} // namespace silica
