#pragma once

#include "Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace silica {

// A select box. Clicking the closed box expands the option list; while open the
// widget's hit area also covers the list (Contains is extended), so clicks land
// on it even though the list is drawn outside the box. A high default z-order
// keeps the list above later siblings. Selecting collapses the list and reports
// the index through onChange / the named handler; clicking elsewhere or losing
// focus closes it too.
class SilicaDropdown : public SilicaWidget {
public:
    SilicaDropdown();

    std::vector<std::string>                    options;
    int                                         selected   = 0;
    float                                       itemHeight = 20.f;
    float                                       fontSize   = .9f;
    Color                                       accent{.42f, .76f, .29f, 1.f};
    std::function<void(int, std::string const&)> onChange;

    // Internal metrics exposed to the declarative layer.
    float itemPadding    = 8.f;
    Color arrowColor     = Color{.75f, .78f, .80f, 1.f};
    Color listBackground = Color{.09f, .10f, .12f, .98f};
    Color selectionColor = Color{.42f, .76f, .29f, .30f};

    // Glyph painted on the right of the closed box; the open list swaps in
    // arrowOpen. Either side can be empty to hide the arrow.
    std::string arrowClosed = "v";
    std::string arrowOpen   = "v";

    [[nodiscard]] bool IsOpen() const { return m_open; }
    void               SetOpen(bool open);
    void               Select(int index, bool notify = true);
    [[nodiscard]] std::string const& SelectedText() const;

    [[nodiscard]] bool Contains(float px, float py) const override;
    [[nodiscard]] bool Focusable() const override { return true; }
    [[nodiscard]] bool HasState(std::string_view name) const override;
    bool OnMouseDown(float x, float y) override;
    bool OnMouseUp(float x, float y, bool inside) override;
    bool OnActivate() override;
    void OnFocusChanged(bool isFocused) override;
    bool OnKeyDown(int keyCode) override;

    void Update() override;
    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

private:
    void                       notify();
    void                       parseOptions(std::string_view value);
    [[nodiscard]] float        listHeight() const;
    [[nodiscard]] int          indexAt(float py) const;
    [[nodiscard]] std::string const& currentText() const;

    bool        m_open = false;
    std::string m_handler;
    std::string m_empty;
};

} // namespace silica
