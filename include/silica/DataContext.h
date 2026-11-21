#pragma once

#include "Variant.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace silica {

// Lexical scope for declarative templates, one level per dynamic list item.
// A ForEach creates a child context with `item` and `index` injected; nested
// lookups walk the parent chain, so an inner list can still read the outer
// row's fields.
//
// Contexts are immutable once built: changing data stores a new container and
// rebuilds the rows, which keeps every widget's view of its scope consistent.
class SilDataContext {
public:
    using Ptr = std::shared_ptr<SilDataContext const>;

    SilDataContext() = default;
    explicit SilDataContext(Ptr parent) : m_parent(std::move(parent)) {}

    void Set(std::string name, Variant value) { m_values[std::move(name)] = std::move(value); }

    [[nodiscard]] bool Has(std::string_view name) const {
        if (m_values.find(name) != m_values.end()) return true;
        return m_parent && m_parent->Has(name);
    }

    [[nodiscard]] Variant Get(std::string_view name) const {
        auto const found = m_values.find(name);
        if (found != m_values.end()) return found->second;
        return m_parent ? m_parent->Get(name) : Variant{};
    }

    [[nodiscard]] Ptr Parent() const { return m_parent; }

private:
    Ptr                 m_parent;
    std::map<std::string, Variant, std::less<>> m_values;
};

} // namespace silica
