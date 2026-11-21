#pragma once

#include "Container.h"
#include "Expression.h"
#include "SilNode.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace silica {

class SilDataContext;

// Declarative list repeater (`ForEach (data: @rows, item: @row, index: @i)`).
//
// The data expression yields an array; every element builds one instance of
// the template subtree with a child scope injecting `item` and `index`. When
// the store key behind the expression is set to a new array the rows are
// rebuilt. Rebuilds are queued and run at the next Update, so a click script
// inside a row can finish before the row it lives in is destroyed.
//
// The rows are ordinary children of the container, so the surrounding layout
// (vertical by default) places them like hand-written widgets.
class SilicaForEach : public SilicaContainer {
public:
    using TemplateBuilder = std::function<std::vector<WidgetPtr>(
        std::shared_ptr<SilDataContext const> const& scope,
        std::vector<SilDiagnostic>&                   diagnostics
    )>;

    SilicaForEach();

    void SetTemplate(TemplateBuilder builder);

    // Parses the data expression and the scope names (with or without '@'),
    // subscribes to its store roots and builds the initial rows. Diagnostics
    // from the initial build are appended to `diagnostics` when provided.
    bool Configure(
        std::string_view               dataExpression,
        std::string_view               itemName,
        std::string_view               indexName,
        std::string&                   error,
        std::vector<SilDiagnostic>*    diagnostics = nullptr
    );

    // Marks the rows stale; Update rebuilds them.
    void Invalidate() { m_dirty = true; }

    void OnDataChanged(std::string_view key) override;
    void Update() override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

private:
    void RebuildNow(std::vector<SilDiagnostic>* diagnostics);

    TemplateBuilder          m_template;
    Expression               m_dataExpression;
    std::vector<std::string> m_dataKeys;
    std::string              m_itemName  = "item";
    std::string              m_indexName = "index";
    bool                     m_dirty     = false;
    bool                     m_userHeight = false;
};

} // namespace silica
