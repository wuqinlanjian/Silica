#include "silica/ForEach.h"

#include "silica/DataContext.h"
#include "silica/Property.h"
#include "silica/Runtime.h"

#include <algorithm>
#include <utility>

namespace silica {
namespace {

// A bound on rows keeps one runaway array from turning the UI thread into an
// allocation loop; the excess is dropped and reported once per rebuild.
constexpr std::size_t kMaxRows = 512;

// The scope name is written `@item` in .sil but stored without the '@', the
// same form Expression records and ResolveValue looks up.
std::string ScopeName(std::string_view text, std::string_view fallback) {
    std::string name(property::Trim(text));
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    if (name.empty()) name = std::string(fallback);
    return name;
}

} // namespace

SilicaForEach::SilicaForEach() {
    // A list reads top-to-bottom unless the document says otherwise.
    layout = LayoutMode::Vertical;
}

void SilicaForEach::SetTemplate(TemplateBuilder builder) { m_template = std::move(builder); }

bool SilicaForEach::Configure(
    std::string_view            dataExpression,
    std::string_view            itemName,
    std::string_view            indexName,
    std::string&                error,
    std::vector<SilDiagnostic>* diagnostics
) {
    auto parsed = Expression::Parse(dataExpression, error);
    if (!parsed) return false;

    m_dataExpression = std::move(*parsed);
    m_dataKeys       = m_dataExpression.Identifiers();
    m_itemName       = ScopeName(itemName, "item");
    m_indexName      = ScopeName(indexName, "index");

    // Roots provided by an outer ForEach never change in the store; the outer
    // rebuild replaces this whole subtree instead.
    std::vector<std::string> watched;
    for (auto const& key : m_dataKeys) {
        auto const& outer = DataContext();
        if (outer && outer->Has(key)) continue;
        watched.push_back(key);
    }
    WatchKeys(watched);

    m_dirty = false;
    RebuildNow(diagnostics);
    return true;
}

void SilicaForEach::OnDataChanged(std::string_view key) {
    SilicaContainer::OnDataChanged(key);
    for (auto const& watched : m_dataKeys) {
        if (watched == key) {
            m_dirty = true;
            return;
        }
    }
}

void SilicaForEach::Update() {
    if (m_dirty) RebuildNow(nullptr);
    SilicaContainer::Update();
}

bool SilicaForEach::ApplyProperty(std::string_view key, std::string_view value) {
    if (property::ToLower(property::Trim(key)) == "height") m_userHeight = true;
    return SilicaContainer::ApplyProperty(key, value);
}

void SilicaForEach::RebuildNow(std::vector<SilDiagnostic>* diagnostics) {
    m_dirty = false;
    Clear();

    if (!m_template || m_dataExpression.Empty()) return;

    Variant const data = m_dataExpression.Evaluate(
        [this](std::string_view name) { return ResolveValue(name); }
    );

    // Anything that is not an array yields no rows (an empty list, or data the
    // model has not filled in yet).
    auto const& items = data.AsArray();

    std::size_t const rowCount = std::min(items.size(), kMaxRows);
    if (items.size() > kMaxRows) {
        runtime::Log(
            "silica: ForEach truncated " + std::to_string(items.size()) + " rows to "
            + std::to_string(kMaxRows) + " (the data array is too large)"
        );
    }

    std::vector<SilDiagnostic> local;
    for (std::size_t i = 0; i < rowCount; ++i) {
        auto scope = std::make_shared<SilDataContext>(DataContext());
        if (!m_itemName.empty()) scope->Set(m_itemName, items[i]);
        if (!m_indexName.empty()) scope->Set(m_indexName, Variant{static_cast<double>(i)});

        for (auto& row : m_template(scope, local)) {
            if (row) AddChild(std::move(row));
        }
    }

    if (diagnostics) {
        diagnostics->insert(diagnostics->end(), local.begin(), local.end());
    } else {
        for (auto const& diagnostic : local) {
            runtime::Log(FormatDiagnostic(diagnostic, "<ForEach>"));
        }
    }

    // Let the next layout pass recompute the height from the new rows; an
    // explicit height in the document stays authoritative.
    if (!m_userHeight) height = 0.f;
}

} // namespace silica
