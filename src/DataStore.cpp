#include "silica/DataStore.h"

#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace silica {
namespace {

using ObserverList = std::vector<std::weak_ptr<DataObserver>>;

std::mutex& Mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, Variant>& Values() {
    static std::unordered_map<std::string, Variant> values;
    return values;
}

std::unordered_map<std::string, ObserverList>& Observers() {
    static std::unordered_map<std::string, ObserverList> observers;
    return observers;
}

// Collects live observers for `key` and drops the expired entries. Must be
// called with the lock held; the returned handles are used after unlocking.
std::vector<std::shared_ptr<DataObserver>> TakeObservers(std::string const& key) {
    std::vector<std::shared_ptr<DataObserver>> live;
    auto const                                 found = Observers().find(key);
    if (found == Observers().end()) return live;

    ObserverList& list = found->second;
    for (auto entry = list.begin(); entry != list.end();) {
        if (auto observer = entry->lock()) {
            live.push_back(std::move(observer));
            ++entry;
        } else {
            entry = list.erase(entry);
        }
    }
    if (list.empty()) Observers().erase(found);
    return live;
}

} // namespace

void DataStore::Set(std::string key, Variant value) {
    std::vector<std::shared_ptr<DataObserver>> observers;
    {
        std::lock_guard lock(Mutex());
        auto&           values = Values();
        auto const      found  = values.find(key);
        if (found != values.end() && found->second == value) return;
        values[key] = std::move(value);
        observers   = TakeObservers(key);
    }
    // Outside the lock: handlers commonly read the store and may write it.
    for (auto const& observer : observers) observer->OnDataChanged(key);
}

Variant DataStore::Get(std::string_view key) {
    std::lock_guard lock(Mutex());
    auto const      found = Values().find(std::string(key));
    return found == Values().end() ? Variant{} : found->second;
}

bool DataStore::Has(std::string_view key) {
    std::lock_guard lock(Mutex());
    return Values().contains(std::string(key));
}

bool DataStore::Remove(std::string_view key) {
    std::string                                owned(key);
    std::vector<std::shared_ptr<DataObserver>> observers;
    {
        std::lock_guard lock(Mutex());
        if (Values().erase(owned) == 0) return false;
        observers = TakeObservers(owned);
    }
    for (auto const& observer : observers) observer->OnDataChanged(owned);
    return true;
}

void DataStore::Clear() {
    std::lock_guard lock(Mutex());
    Values().clear();
}

std::size_t DataStore::Size() {
    std::lock_guard lock(Mutex());
    return Values().size();
}

void DataStore::Subscribe(std::string_view key, std::shared_ptr<DataObserver> observer) {
    if (key.empty() || !observer) return;
    std::lock_guard lock(Mutex());
    ObserverList&   list = Observers()[std::string(key)];
    for (auto const& entry : list) {
        if (entry.lock() == observer) return;
    }
    list.push_back(std::move(observer));
}

void DataStore::Unsubscribe(std::string_view key, DataObserver const* observer) {
    std::lock_guard lock(Mutex());
    auto const      found = Observers().find(std::string(key));
    if (found == Observers().end()) return;

    ObserverList& list = found->second;
    for (auto entry = list.begin(); entry != list.end();) {
        auto current = entry->lock();
        if (!current || current.get() == observer) entry = list.erase(entry);
        else ++entry;
    }
    if (list.empty()) Observers().erase(found);
}

} // namespace silica
