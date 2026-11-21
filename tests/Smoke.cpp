// Compile-only check of the public API: this tree is what a mod builds before
// handing the root to the renderer.

#include "silica/Silica.h"

using namespace silica;

static std::shared_ptr<SilicaContainer> buildDemoTree() {
    auto root = std::make_shared<SilicaContainer>();
    root->x      = 40.f;
    root->y      = 30.f;
    root->width  = 400.f;
    root->height = 250.f;
    root->ApplyProperty("layout", "vertical");
    root->ApplyProperty("padding", "10");
    root->ApplyProperty("background", "#0E0F11EA");
    root->ApplyProperty("border", "1 #5A5B5CE6");

    auto title = std::make_shared<SilicaLabel>();
    title->ApplyProperty("text", "Silica");
    title->ApplyProperty("font-size", "1.2");
    root->Add(title)->margin = 6.f;

    auto panel = root->Add(std::make_shared<SilicaPanel>());
    panel->height = 120.f;
    panel->style  = UIStyle::Outline();

    auto button = std::make_shared<SilicaButton>();
    button->ApplyProperty("text", "确定");
    button->ApplyProperty("background", "#4CAF50");
    button->ApplyProperty("onClick", "demo");
    button->margin = 8.f;
    root->Add(button);

    auto hidden = std::make_shared<SilicaLabel>();
    hidden->visible = false;
    root->Add(hidden);

    root->Update();
    return root;
}

void renderTree(SilicaContainer& root, MinecraftUIRenderContext& context) { root.Render(context); }
