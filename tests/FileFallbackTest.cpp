// Host-side test for the file-loading layer: root-relative resolution, the
// embedded fallback for missing/broken documents, and ReloadUI. It never talks
// to the game, so the renderer entry points are stubbed out below.

#include "silica/Silica.h"
#include "silica/Runtime.h"
#include "silica/Animation.h"
#include "silica/Button.h"
#include "silica/Checkbox.h"
#include "silica/DataStore.h"
#include "silica/Dropdown.h"
#include "silica/Image.h"
#include "silica/Label.h"
#include "silica/Panel.h"
#include "silica/Part.h"
#include "silica/ScrollView.h"
#include "silica/Slider.h"
#include "silica/TextBox.h"
#include "silica/UICompiler.h"
#include "silica/WidgetFactory.h"

#include <windows.h>
#undef DrawText // windows.h maps the name onto DrawTextW

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace silica {

Font* ResolveFont(MinecraftUIRenderContext&, std::string_view) { return nullptr; }

float TextWidth(MinecraftUIRenderContext&, std::string_view, float, std::string_view) { return 0.f; }

void FillRect(MinecraftUIRenderContext&, float, float, float, float, Color) {}

void DrawFrame(MinecraftUIRenderContext&, float, float, float, float, Color, float) {}

void FillRoundRect(MinecraftUIRenderContext&, float, float, float, float, float, Color) {}

void DrawRoundFrame(MinecraftUIRenderContext&, float, float, float, float, float, Color, float) {}

void FillGradientRect(MinecraftUIRenderContext&, float, float, float, float, Color, Color, bool) {}

void FillRoundGradientRect(
    MinecraftUIRenderContext&,
    float,
    float,
    float,
    float,
    float,
    Color,
    Color,
    bool
) {}

void FillLinearGradient(MinecraftUIRenderContext&, float, float, float, float, GradientSpec const&) {}

void FillRoundLinearGradient(
    MinecraftUIRenderContext&,
    float,
    float,
    float,
    float,
    float,
    GradientSpec const&
) {}

void DrawShadow(MinecraftUIRenderContext&, float, float, float, float, float, ShadowSpec const&, float) {}

bool DrawNineSlice(
    MinecraftUIRenderContext&,
    std::string_view,
    float,
    float,
    float,
    float,
    NineSliceSpec const&,
    Color,
    float
) {
    return false;
}

void DrawText(
    MinecraftUIRenderContext&,
    float,
    float,
    float,
    std::string_view,
    Color,
    float,
    ::ui::TextAlignment,
    std::string_view
) {}

void FlushText(MinecraftUIRenderContext&) {}

void FlushDraws(MinecraftUIRenderContext&) {}

bool RectVisibleInClip(float, float, float, float) { return true; }

void PushClip(MinecraftUIRenderContext&, float, float, float, float) {}

void PopClip(MinecraftUIRenderContext&) {}

bool DrawTexture(
    MinecraftUIRenderContext&,
    std::string_view,
    float,
    float,
    float,
    float,
    Color,
    float
) {
    return false;
}

} // namespace silica

namespace {

constexpr char kFallback[] = R"(
Container {
    name: from_fallback;
    Label { text: "fallback"; }
}
)";

int                      g_failures = 0;
std::vector<std::string> g_log;

void check(bool condition, char const* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++g_failures;
}

void writeFile(std::filesystem::path const& path, std::string const& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string nameOf(silica::WidgetPtr const& widget) {
    return widget ? widget->name : std::string("<none>");
}

} // namespace

int main() {
    std::filesystem::path const      dir = std::filesystem::path("build") / "file_fallback_test";
    std::error_code                  ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    silica::runtime::SetLogSink([](std::string_view line) { g_log.emplace_back(line); });
    silica::Silica::SetRootPath(dir.string());
    check(
        silica::Silica::ResolvePath("ui/a.sil") == (dir / "ui" / "a.sil").generic_string(),
        "relative paths join the root"
    );

    writeFile(dir / "ui" / "demo.sil", R"(
Container {
    name: from_file;
    Label { text: "hello"; }
}
)");

    auto fromFile = silica::Silica::CompileFile("ui/demo.sil", kFallback);
    check(nameOf(fromFile) == "from_file", "file wins over the fallback");

    auto fromAbsolute =
        silica::Silica::CompileFile(std::filesystem::absolute(dir / "ui" / "demo.sil").string(), kFallback);
    check(nameOf(fromAbsolute) == "from_file", "absolute paths bypass the root");

    g_log.clear();
    auto missing = silica::Silica::CompileFile("ui/nope.sil", kFallback);
    check(nameOf(missing) == "from_fallback", "missing file compiles the fallback");
    check(!g_log.empty(), "missing file is reported to the log");

    writeFile(dir / "ui" / "broken.sil", R"(
Nope {
    text: "x";
}
)");
    g_log.clear();
    auto broken = silica::Silica::CompileFile("ui/broken.sil", kFallback);
    check(nameOf(broken) == "from_fallback", "broken file compiles the fallback");
    check(!g_log.empty(), "broken file is reported to the log");

    writeFile(dir / "ui" / "live.sil", "Container { name: v1; }");
    auto first = silica::Silica::CompileFile("ui/live.sil", kFallback);
    check(nameOf(first) == "v1", "live document loads");

    writeFile(dir / "ui" / "live.sil", "Container { name: v2; }");
    auto second = silica::Silica::ReloadUI("ui/live.sil");
    check(nameOf(second) == "v2", "reload picks up the edited file");
    check(nameOf(silica::Silica::CurrentUI("ui/live.sil")) == "v2", "CurrentUI returns the reloaded tree");

    writeFile(dir / "ui" / "live.sil", "Nope {");
    auto afterBreak = silica::Silica::ReloadUI("ui/live.sil");
    check(nameOf(afterBreak) == "v2", "failed reload keeps the previous tree");
    check(silica::Silica::CurrentUI("ui/live.sil") == afterBreak, "the previous tree stays mounted");

    check(silica::Silica::ReloadUI("ui/never_loaded.sil") == nullptr, "reload of an unknown path is a no-op");

    // Input layer: hit testing, click dispatch, keyboard focus.
    writeFile(dir / "ui" / "input.sil", R"(
Container {
    width: 200;
    height: 100;
    layout: vertical;
    Button {
        name: click_me;
        text: "x";
        width: 100;
        height: 24;
        onClick: "test_click";
    }
}
)");

    int clicks = 0;
    silica::SilicaHandlers::Register("test_click", [&clicks] { ++clicks; });

    auto inputRoot = silica::Silica::CompileFile("ui/input.sil", kFallback);
    inputRoot->Update();
    auto button = silica::FindByName(inputRoot, "click_me");
    check(button != nullptr, "named widget is found");
    check(silica::HitTest(inputRoot, 50.f, 10.f) == button, "hit test finds the button");
    check(silica::HitTest(inputRoot, 300.f, 10.f) == nullptr, "hit test misses outside the tree");

    silica::PointerRouter router;
    silica::PointerState press;
    press.x       = 50.f;
    press.y       = 10.f;
    press.down    = true;
    press.pressed = true;
    router.Update(inputRoot, press);

    silica::PointerState release;
    release.x        = 50.f;
    release.y        = 10.f;
    release.released = true;
    router.Update(inputRoot, release);
    check(clicks == 1, "press and release clicks the button");

    silica::FocusRing ring;
    ring.Collect(inputRoot);
    check(ring.Size() == 1, "focus ring collects buttons");
    ring.Activate();
    check(clicks == 2, "keyboard activation clicks the focused button");

    // Stage 1: Image properties and ScrollView clipping/scrolling.
    auto imageWidget = silica::WidgetFactory::Create("Image");
    auto icon        = std::dynamic_pointer_cast<silica::SilicaImage>(imageWidget);
    check(icon != nullptr, "Image is registered in the factory");
    check(imageWidget && imageWidget->ApplyProperty("src", "textures/items/diamond"), "Image accepts src");
    check(icon && icon->src == "textures/items/diamond", "Image stores the texture path");
    if (imageWidget) imageWidget->Update();
    check(imageWidget && imageWidget->width == 16.f && imageWidget->height == 16.f, "Image defaults to 16x16");

    writeFile(dir / "ui" / "scroll.sil", R"(
ScrollView {
    name: scroller;
    width: 120;
    height: 40;
    Label { name: row_a; text: "a"; height: 24; }
    Label { name: row_b; text: "b"; height: 24; }
    Label { name: row_c; text: "c"; height: 24; }
}
)");

    auto scrollRoot = silica::Silica::CompileFile("ui/scroll.sil", kFallback);
    auto scroller   = std::dynamic_pointer_cast<silica::SilicaScrollView>(scrollRoot);
    check(scroller != nullptr, "ScrollView compiles from .sil");
    check(scroller && scroller->Count() == 3, "ScrollView children live in the content container");
    if (!scrollRoot) {
        std::printf("FILE FALLBACK TEST FAIL\n");
        return 1;
    }
    scrollRoot->Update();

    auto rowA = silica::FindByName(scrollRoot, "row_a");
    auto rowB = silica::FindByName(scrollRoot, "row_b");
    auto rowC = silica::FindByName(scrollRoot, "row_c");
    check(rowA && rowB && rowC, "scrolled content is findable by name");
    check(rowA && std::abs(rowA->y) < .01f, "content starts at the viewport top");
    check(silica::HitTest(scrollRoot, 10.f, 10.f) == rowA, "hit test reaches visible content");
    check(silica::HitTest(scrollRoot, 10.f, 60.f) == nullptr, "content below the viewport is clipped");

    silica::PointerRouter wheelRouter;
    silica::PointerState wheel;
    wheel.x = 10.f;
    wheel.y = 10.f;
    scroller->smoothScroll = false; // assert the clamped target directly
    check(wheelRouter.Wheel(scrollRoot, wheel, -1.f), "ScrollView claims the wheel");
    check(std::abs(scroller->scrollOffsetY - 24.f) < .01f, "one notch scrolls one step");
    check(rowA && std::abs(rowA->y + 24.f) < .01f, "content moves up with the offset");
    check(silica::HitTest(scrollRoot, 10.f, 10.f) == rowB, "hit test follows the scrolled content");

    wheelRouter.Wheel(scrollRoot, wheel, -1.f);
    check(std::abs(scroller->scrollOffsetY - 32.f) < .01f, "offset clamps to the content remainder");
    check(silica::HitTest(scrollRoot, 10.f, 39.f) == rowC, "last row reaches the viewport bottom");

    wheelRouter.Wheel(scrollRoot, wheel, 5.f);
    check(std::abs(scroller->scrollOffsetY) < .01f, "wheel back up clamps at zero");

    // Smooth scrolling: the wheel only moves the target and Update eases toward
    // it, so content and thumb glide instead of snapping.
    scroller->smoothScroll    = true;
    scroller->scrollSmoothing = 22.f;
    wheelRouter.Wheel(scrollRoot, wheel, -1.f);
    check(std::abs(scroller->scrollTargetY - 24.f) < .01f, "wheel moves the scroll target");
    check(
        scroller->scrollOffsetY > 0.f && scroller->scrollOffsetY < 24.f,
        "content eases toward its target"
    );
    for (int i = 0; i < 240; ++i) scroller->Update();
    check(std::abs(scroller->scrollOffsetY - 24.f) < .3f, "smoothing settles on the target");

    // Scrollbar drag: the thumb is grabbed with the press, moves the target and
    // is released with the button.
    scroller->smoothScroll  = false;
    scroller->scrollTargetY = 0.f;
    scroller->scrollOffsetY = 0.f;
    scroller->Update();

    silica::PointerRouter dragRouter;
    silica::PointerState  barPress;
    barPress.x       = 118.f;
    barPress.y       = 6.f;
    barPress.down    = true;
    barPress.pressed = true;
    dragRouter.Update(scrollRoot, barPress);
    check(dragRouter.Captured() == scroller.get(), "pressing the scrollbar grabs it");

    silica::PointerState barDrag = barPress;
    barDrag.pressed = false;
    barDrag.y       = 34.f;
    dragRouter.Update(scrollRoot, barDrag);
    check(scroller->scrollTargetY > 30.f, "dragging the thumb targets the bottom");
    check(
        std::abs(scroller->scrollOffsetY - scroller->scrollTargetY) < .01f,
        "instant mode follows the drag"
    );

    silica::PointerState barRelease;
    barRelease.x        = 118.f;
    barRelease.y        = 34.f;
    barRelease.released = true;
    dragRouter.Update(scrollRoot, barRelease);
    check(!scroller->DraggingScrollbar(), "releasing the scrollbar ends the drag");

    // Structure-level skins: any widget accepts Part decor layers, which anchor
    // to its box and obey the owner's state conditions.
    {
        auto cb = std::make_shared<silica::SilicaCheckbox>();
        cb->x      = 10.f;
        cb->y      = 20.f;
        cb->width  = 100.f;
        cb->height = 24.f;

        auto track = std::make_shared<silica::SilicaPart>();
        check(track->ApplyProperty("anchor", "left"), "part anchor parses");
        track->ApplyProperty("width", "30");
        track->ApplyProperty("height", "20");
        track->ApplyProperty("inset-left", "2");
        track->ApplyProperty("when", "checked");

        auto plain = std::make_shared<silica::SilicaPanel>();
        check(cb->AddChild(track), "widgets accept parts as children");
        check(!cb->AddChild(plain), "leaves still reject normal children");
        check(cb->Parts().size() == 1, "parts are stored on the owner");

        check(!track->Place(*cb), "part hides while its condition is false");
        check(!track->visible, "a hidden part is not painted");

        cb->checked = true;
        check(track->Place(*cb), "part shows when the owner state matches");
        check(
            track->visible && std::abs(track->x - 12.f) < .01f && std::abs(track->y - 22.f) < .01f,
            "left part is inset and vertically centered"
        );
        check(
            std::abs(track->width - 30.f) < .01f && std::abs(track->height - 20.f) < .01f,
            "explicit part size is kept"
        );

        auto fill = std::make_shared<silica::SilicaPart>();
        fill->insetLeft = fill->insetTop = fill->insetRight = fill->insetBottom = 4.f;
        cb->AddChild(fill);
        check(fill->Place(*cb), "fill part always shows");
        check(
            std::abs(fill->x - 14.f) < .01f && std::abs(fill->width - 92.f) < .01f,
            "fill part follows the owner box"
        );

        auto skinRoot = silica::UICompiler::Compile(R"(
Checkbox {
    name: sw;
    width: 120;
    height: 22;
    box-size: 40;
    show-box: false;
    Part { anchor: left; width: 40; height: 20; radius: 10; background: #333333; when: "!checked"; }
    Part { anchor: left; width: 40; height: 20; radius: 10; background: #498FC3; when: "checked"; }
}
)");
        auto sw = std::dynamic_pointer_cast<silica::SilicaCheckbox>(skinRoot);
        check(sw && !sw->showBox && sw->Parts().size() == 2, ".sil compiles parts onto a checkbox");
        auto first = sw && !sw->Parts().empty()
            ? std::dynamic_pointer_cast<silica::SilicaPart>(sw->Parts()[0])
            : nullptr;
        check(
            first && first->when == "!checked" && first->anchor == silica::SilicaPart::Anchor::Left,
            "part condition and anchor come from the document"
        );
        check(first && std::abs(first->style.radius - 10.f) < .01f, "part keeps style properties");
    }

    // State styles and rounded corners are ordinary declarative properties.
    {
        auto styled = silica::UICompiler::Compile(R"(
Container {
    name: styled;
    background: #112233;
    border: 2 solid #445566;
    border-radius: 8;
    hover-background: #223344;
    pressed-background: #334455;
}
)");
        check(styled != nullptr, "state style properties compile");
        check(styled && std::abs(styled->style.radius - 8.f) < .01f, "border-radius is parsed");
        check(styled && styled->style.hoverBackground.has_value(), "hover-background is stored");
        check(styled && styled->style.pressedBackground.has_value(), "pressed-background is stored");

        auto cleared = silica::UICompiler::Compile(R"(
Container {
    border-radius: 4;
    hover-background: none;
    pressed-background: none;
}
)");
        check(cleared && std::abs(cleared->style.radius - 4.f) < .01f, "border-radius keeps its value");
        check(
            cleared && !cleared->style.hoverBackground && !cleared->style.pressedBackground,
            "state colors accept 'none' to clear"
        );

        auto tolerant = silica::UICompiler::Compile(R"(
Style Wide-Gap { padding: 7; }
Container { name: tolerant; class: "wide_gap"; }
)");
        check(
            tolerant && std::abs(tolerant->padding - 7.f) < .01f,
            "class names ignore case and word separators"
        );
    }

    // Gradients, shadows, fonts, border images and text alignment.
    {
        auto fancy = silica::UICompiler::Compile(R"(
Container {
    name: fancy;
    gradient: horizontal #101010 #303030;
    shadow: 2 4 8 2 #00000099;
    font-family: "Mojangles";
    text-align: center;
    border-image: "textures/ui/dialog_background_opaque" 8 6 8 6;
}
)");
        check(fancy && fancy->style.gradient && fancy->style.gradient->horizontal, "gradient direction is parsed");
        check(
            fancy && fancy->style.gradient && std::abs(fancy->style.gradient->from.r - 16.f / 255.f) < .01f
                && std::abs(fancy->style.gradient->to.r - 48.f / 255.f) < .01f,
            "gradient keeps both stops"
        );
        check(
            fancy && fancy->style.shadow && std::abs(fancy->style.shadow->dx - 2.f) < .01f
                && std::abs(fancy->style.shadow->dy - 4.f) < .01f
                && std::abs(fancy->style.shadow->blur - 8.f) < .01f
                && std::abs(fancy->style.shadow->spread - 2.f) < .01f,
            "shadow keeps offset, blur and spread"
        );
        check(
            fancy && fancy->style.shadow && fancy->style.shadow->color.a > .55f
                && fancy->style.shadow->color.a < .65f,
            "shadow keeps its color alpha"
        );
        check(fancy && fancy->style.fontFamily == "Mojangles", "font-family is stored");
        check(fancy && fancy->style.textAlign == 1, "text-align center is stored");
        check(
            fancy && fancy->style.borderImage
                && fancy->style.borderImage->src == "textures/ui/dialog_background_opaque",
            "border-image keeps its texture path"
        );
        check(
            fancy && fancy->style.borderImage && std::abs(fancy->style.borderImage->top - 8.f) < .01f
                && std::abs(fancy->style.borderImage->right - 6.f) < .01f
                && std::abs(fancy->style.borderImage->bottom - 8.f) < .01f
                && std::abs(fancy->style.borderImage->left - 6.f) < .01f,
            "border-image slices follow CSS order"
        );

        auto plain = silica::UICompiler::Compile(R"(
Container {
    gradient: none;
    shadow: none;
    border-image: none;
    text-align: right;
}
)");
        check(
            plain && !plain->style.gradient && !plain->style.shadow && !plain->style.borderImage,
            "decorations accept 'none' to clear"
        );
        check(plain && plain->style.textAlign == 2, "text-align right is stored");
    }

    // Per-control internal details.
    {
        auto details = silica::UICompiler::Compile(R"(
Container {
    Checkbox { name: cb; box-size: 18; text-gap: 10; check-inset: 4; }
    Slider { name: sl; track-height: 6; track-color: #112233; knob-color: #445566; knob-active-color: #778899;
             smooth: true; smoothing: 18; }
    TextBox { name: tb; text-padding: 7; caret-width: 2; caret-color: #FF0000; placeholder-color: #00FF00; }
    Dropdown { name: dd; arrow-color: #FFAA00; list-background: #223344; selection-color: #556677; item-padding: 12;
               arrow: ">" "∨"; }
    ScrollView { name: sv; scrollbar-width: 5; scrollbar-inset: 2; scrollbar-radius: 2;
                 scrollbar-track-color: #111111; scrollbar-thumb-color: #222222; }
}
)");
        auto cb = std::dynamic_pointer_cast<silica::SilicaCheckbox>(silica::FindByName(details, "cb"));
        auto sl = std::dynamic_pointer_cast<silica::SilicaSlider>(silica::FindByName(details, "sl"));
        auto tb = std::dynamic_pointer_cast<silica::SilicaTextBox>(silica::FindByName(details, "tb"));
        auto dd = std::dynamic_pointer_cast<silica::SilicaDropdown>(silica::FindByName(details, "dd"));
        auto sv = std::dynamic_pointer_cast<silica::SilicaScrollView>(silica::FindByName(details, "sv"));
        check(
            cb && std::abs(cb->boxSizeOverride - 18.f) < .01f && std::abs(cb->textGap - 10.f) < .01f
                && std::abs(cb->checkInset - 4.f) < .01f,
            "checkbox metrics are configurable"
        );
        check(
            sl && std::abs(sl->trackHeight - 6.f) < .01f && sl->trackColor.r < .1f && sl->knobColor.r > .2f
                && sl->knobColor.r < .3f && sl->knobActiveColor.r > .4f,
            "slider track and knob colors are configurable"
        );
        check(
            sl && sl->smooth && std::abs(sl->smoothing - 18.f) < .01f,
            "slider smoothing comes from the document"
        );
        check(
            tb && std::abs(tb->textPadding - 7.f) < .01f && std::abs(tb->caretWidth - 2.f) < .01f
                && tb->caretColor.r > .9f && tb->placeholderColor.g > .9f,
            "text box padding, caret and placeholder are configurable"
        );
        check(
            dd && std::abs(dd->itemPadding - 12.f) < .01f && dd->arrowColor.r > .9f && dd->arrowColor.b < .2f
                && dd->selectionColor.r > .3f,
            "dropdown arrow, list and selection colors are configurable"
        );
        check(
            dd && dd->arrowClosed == ">" && dd->arrowOpen == "∨",
            "dropdown arrow glyphs swap between closed and open states"
        );
        check(
            sv && std::abs(sv->scrollbarWidth - 5.f) < .01f && std::abs(sv->scrollbarInset - 2.f) < .01f
                && std::abs(sv->scrollbarRadius - 2.f) < .01f && sv->scrollbarThumbColor.r < .2f,
            "scrollbar metrics are configurable"
        );
    }

    // The test mod's real document is compiled here too: it catches .sil
    // syntax and property drift without launching the game.
    auto const panelPath = std::filesystem::path("..") / "SilicaTest" / "ui" / "panel.sil";
    if (std::filesystem::exists(panelPath)) {
        auto panelResult = silica::UICompiler::CompileFile(std::filesystem::absolute(panelPath).string());
        auto panel       = panelResult.root;
        check(panel != nullptr, "SilicaTest panel document compiles");

        bool reactiveWarnings = false;
        for (auto const& diagnostic : panelResult.diagnostics) {
            if (diagnostic.message.find("cannot attach the script") != std::string::npos
                || diagnostic.message.find("cannot bind property") != std::string::npos) {
                reactiveWarnings = true;
                std::printf("     %s:%d: %s\n", panelPath.string().c_str(), diagnostic.line, diagnostic.message.c_str());
            }
        }
        check(!reactiveWarnings, "panel scripts and bindings attach cleanly");

        // The mod swaps in a minimal fallback tree whenever the real document
        // reports an error, so any error diagnostic here would ship a broken UI.
        bool panelErrors = false;
        for (auto const& diagnostic : panelResult.diagnostics) {
            if (diagnostic.severity == silica::SilDiagnostic::Severity::Error) {
                panelErrors = true;
                std::printf(
                    "     %s:%d:%d: %s\n",
                    panelPath.string().c_str(),
                    diagnostic.line,
                    diagnostic.column,
                    diagnostic.message.c_str()
                );
            }
        }
        check(!panelErrors, "panel document compiles without errors");
        check(
            panel && std::abs(panel->width - 560.f) < .01f && std::abs(panel->height - 352.f) < .01f,
            "panel document keeps its size"
        );
        check(
            panel && panel->style.radius == 0.f && !panel->style.shadow,
            "panel sheet is square and flat"
        );

        auto panelRoot = std::dynamic_pointer_cast<silica::SilicaContainer>(panel);
        check(panelRoot != nullptr && panelRoot->layout == silica::LayoutMode::Vertical, "panel root lays out vertically");
        if (panel) panel->Update();

        auto panelScroller =
            std::dynamic_pointer_cast<silica::SilicaScrollView>(silica::FindByName(panel, "scroll_area"));
        check(panelScroller != nullptr, "panel exposes the scroll area");
        if (panelScroller) panelScroller->Update();
        check(
            panelScroller && panelScroller->ContentHeight() > panelScroller->height,
            "panel content overflows its viewport"
        );
        check(
            panelScroller && std::abs(panelScroller->scrollbarWidth - 3.f) < .01f
                && std::abs(panelScroller->scrollbarRadius) < .01f,
            "panel scrollbar is thin and square"
        );

        auto panelRows =
            std::dynamic_pointer_cast<silica::SilicaContainer>(silica::FindByName(panel, "scroll_content"));
        check(panelRows && panelRows->Count() == 25, "panel scroll content holds every row");

        // Document-level reuse: both component instances and the wrapped demo
        // label compile into the real panel.
        bool demoRowFound   = false;
        bool slotChildFound = false;
        if (panelRows) {
            for (auto const& child : panelRows->Children()) {
                auto box = std::dynamic_pointer_cast<silica::SilicaContainer>(child);
                if (!box || box->Count() < 2) continue;
                auto title = std::dynamic_pointer_cast<silica::SilicaLabel>(box->Children()[0]);
                if (!title) continue;
                if (title->text == "组件复用") {
                    auto detail = std::dynamic_pointer_cast<silica::SilicaLabel>(box->Children()[1]);
                    demoRowFound = box->Count() == 2 && detail && detail->text == "参数与默认值";
                }
                if (title->text == "Slot 插槽") {
                    auto detail = std::dynamic_pointer_cast<silica::SilicaLabel>(box->Children()[1]);
                    auto slot   = std::dynamic_pointer_cast<silica::SilicaLabel>(box->Children()[2]);
                    slotChildFound = box->Count() == 3 && detail && detail->text == "默认说明" && slot
                                  && slot->text == "← 调用方子节点";
                }
            }
        }
        check(demoRowFound, "panel component row keeps its arguments");
        check(slotChildFound, "panel component row inserts Slot content and defaults");
        auto wrappedDemo = std::dynamic_pointer_cast<silica::SilicaLabel>(silica::FindByName(panel, "wrapped_demo"));
        check(wrappedDemo && wrappedDemo->wrap, "panel document uses an opt-in wrapped label");

        // Disabling is a plain state: a bound data key flips it, HasState
        // exposes it to .sil rules and PaintAlpha applies the style's dim.
        auto lockButton =
            std::dynamic_pointer_cast<silica::SilicaButton>(silica::FindByName(panel, "demo_lock_button"));
        check(lockButton && !lockButton->IsDisabled(), "demo lock button starts enabled");
        if (lockButton) {
            silica::DataStore::Set("demo_locked", true);
            check(lockButton->disabled && lockButton->HasState("disabled"), "a data key disables the button");
            check(
                std::abs(lockButton->PaintAlpha() - lockButton->style.opacity * .85f) < .01f,
                "disabled opacity scales the painted alpha"
            );
            silica::DataStore::Set("demo_locked", false);
            check(!lockButton->IsDisabled(), "clearing the key re-enables the button");
        }

        auto panelCheck = std::dynamic_pointer_cast<silica::SilicaCheckbox>(silica::FindByName(panel, "opt_check"));
        check(
            panelCheck && panelCheck->text == "记住我的选择" && std::abs(panelCheck->boxSizeOverride - 4.f) < .01f
                && !panelCheck->showBox && panelCheck->Parts().size() == 7,
            "panel checkbox row is skinned from parts"
        );
        auto panelSwitch = std::dynamic_pointer_cast<silica::SilicaCheckbox>(silica::FindByName(panel, "opt_switch"));
        check(
            panelSwitch && panelSwitch->text == "启用网络同步" && panelSwitch->checked
                && panelSwitch->Parts().size() == 7,
            "panel switch row keeps its square toggle parts"
        );
        bool squareParts = true;
        if (panelSwitch) {
            for (auto const& part : panelSwitch->Parts()) {
                if (part && part->style.radius > 0.f) squareParts = false;
            }
        }
        check(squareParts, "panel decor stays square");

        auto panelSlider = std::dynamic_pointer_cast<silica::SilicaSlider>(silica::FindByName(panel, "volume"));
        check(
            panelSlider && panelSlider->max == 100.f && panelSlider->smooth
                && std::abs(panelSlider->trackHeight - 3.f) < .01f
                && std::abs(panelSlider->knobSize - 11.f) < .01f && panelSlider->style.radius == 0.f,
            "panel carries a square smooth slider"
        );
        auto panelField = std::dynamic_pointer_cast<silica::SilicaTextBox>(silica::FindByName(panel, "command_field"));
        check(
            panelField && panelField->maxLength == 32 && std::abs(panelField->caretWidth - 1.f) < .01f
                && !panelField->placeholder.empty(),
            "panel carries the text box with its blinking caret"
        );
        auto panelMode = std::dynamic_pointer_cast<silica::SilicaDropdown>(silica::FindByName(panel, "mode"));
        check(
            panelMode && panelMode->options.size() == 3 && std::abs(panelMode->itemPadding - 8.f) < .01f
                && panelMode->arrowClosed == ">" && panelMode->arrowOpen == "∨",
            "panel carries the dropdown with its state arrows"
        );

        auto navGeneral =
            std::dynamic_pointer_cast<silica::SilicaCheckbox>(silica::FindByName(panel, "nav_general"));
        auto navQuality =
            std::dynamic_pointer_cast<silica::SilicaCheckbox>(silica::FindByName(panel, "nav_quality"));
        check(
            navGeneral && navGeneral->checked && navGeneral->Parts().size() == 4,
            "panel sidebar starts on the first category"
        );
        check(navQuality && !navQuality->checked, "panel sidebar selects one category at a time");

        auto panelStatus = silica::FindByName(panel, "status");
        check(panelStatus != nullptr, "panel keeps its status label");
        auto panelHint = silica::FindByName(panel, "hint");
        check(panelHint != nullptr, "panel keeps its toggled hint label");
        auto panelValue = silica::FindByName(panel, "volume_value");
        check(panelValue != nullptr, "panel binds the slider value label");
        auto panelClose = silica::FindByName(panel, "close_button");
        check(panelClose != nullptr && panelClose->style.radius == 0.f, "panel header has a square close button");
        auto panelLogo = std::dynamic_pointer_cast<silica::SilicaImage>(silica::FindByName(panel, "logo"));
        check(
            panelLogo && panelLogo->src == "file:ui/logo.png" && std::abs(panelLogo->width - 16.f) < .01f
                && std::abs(panelLogo->height - 16.f) < .01f,
            "panel header shows an external image file"
        );
    }

    // Stage 2/3 widgets: form controls, callbacks and keyboard editing.
    int         checks      = 0;
    bool        lastChecked = false;
    std::string lastSlider;
    std::string lastText;
    int         lastMode = -1;
    int         submits  = 0;

    silica::SilicaHandlers::Register("test_toggle", [&](std::string_view value) {
        ++checks;
        lastChecked = value == "true";
    });
    silica::SilicaHandlers::Register("test_slider", [&](std::string_view value) { lastSlider = std::string(value); });
    silica::SilicaHandlers::Register("test_text", [&](std::string_view value) { lastText = std::string(value); });
    silica::SilicaHandlers::Register("test_submit", [&](std::string_view) { ++submits; });
    silica::SilicaHandlers::Register("test_mode", [&](std::string_view value) { lastMode = std::atoi(std::string(value).c_str()); });

    writeFile(dir / "ui" / "form.sil", R"(
Container {
    name: form;
    layout: vertical;
    width: 300;
    height: 240;
    Checkbox {
        name: opt;
        text: "启用";
        width: 120;
        height: 18;
        onChange: "test_toggle";
    }
    Slider {
        name: volume;
        width: 200;
        height: 20;
        min: 0;
        max: 100;
        step: 10;
        value: 50;
        onChange: "test_slider";
    }
    TextBox {
        name: name_field;
        text: "abc";
        width: 200;
        height: 22;
        max_length: 5;
        onChange: "test_text";
        onSubmit: "test_submit";
    }
    Dropdown {
        name: mode;
        width: 160;
        height: 24;
        options: "生存, 创造, 冒险";
        selected: 0;
        onChange: "test_mode";
    }
}
)");

    auto formRoot = silica::Silica::CompileFile("ui/form.sil", "");
    check(formRoot != nullptr, "form document compiles");
    check(
        silica::WidgetFactory::Has("TextBox") && silica::WidgetFactory::Has("Dropdown")
            && silica::WidgetFactory::Has("Checkbox") && silica::WidgetFactory::Has("Slider"),
        "form widgets are registered"
    );
    if (!formRoot) {
        std::printf("FILE FALLBACK TEST FAIL\n");
        return 1;
    }
    formRoot->Update();

    auto checkbox = std::dynamic_pointer_cast<silica::SilicaCheckbox>(silica::FindByName(formRoot, "opt"));
    auto slider   = std::dynamic_pointer_cast<silica::SilicaSlider>(silica::FindByName(formRoot, "volume"));
    auto textBox  = std::dynamic_pointer_cast<silica::SilicaTextBox>(silica::FindByName(formRoot, "name_field"));
    auto dropdown = std::dynamic_pointer_cast<silica::SilicaDropdown>(silica::FindByName(formRoot, "mode"));
    check(checkbox && slider && textBox && dropdown, "form widgets are findable by name");
    check(dropdown && dropdown->zOrder >= 100, "dropdown floats above its siblings");
    check(dropdown && dropdown->options.size() == 3, "dropdown splits its option list");
    check(dropdown && dropdown->SelectedText() == "生存", "dropdown starts on the selected option");

    silica::PointerRouter router2;
    auto const clickAt = [&](float px, float py) {
        silica::PointerState press;
        press.x        = px;
        press.y        = py;
        press.down     = true;
        press.pressed  = true;
        router2.Update(formRoot, press);

        silica::PointerState release;
        release.x        = px;
        release.y        = py;
        release.released = true;
        router2.Update(formRoot, release);
    };

    clickAt(10.f, 6.f);
    check(checks == 1 && checkbox->checked, "clicking the checkbox toggles it");
    check(lastChecked, "checkbox reports true through the named handler");
    check(silica::FocusManager::Get().Current() == checkbox.get(), "clicking focuses the checkbox");

    router2.Reset();
    router2.Update(formRoot, silica::PointerState{.x = 10.f, .y = 6.f});
    silica::FocusRing formRing;
    formRing.Collect(formRoot);
    formRing.Activate();
    check(checks == 2 && !checkbox->checked, "keyboard activation toggles the checkbox");

    checkbox->focused = true;
    check(checkbox->OnKeyDown(VK_SPACE) && checks == 3, "Space toggles the checkbox while focused");
    checkbox->focused = false;

    // Slider: press at 30% of the track, drag to the far right, release.
    clickAt(62.f, 28.f);
    check(std::abs(slider->value - 30.f) < .51f, "slider jumps to the pressed position");
    check(lastSlider == "30", "slider formats the value for the handler");

    silica::PointerState grab;
    grab.x       = 62.f;
    grab.y       = 28.f;
    grab.down    = true;
    grab.pressed = true;
    router2.Update(formRoot, grab);

    silica::PointerState drag;
    drag.x    = 500.f;
    drag.y    = 28.f;
    drag.down = true;
    router2.Update(formRoot, drag);
    check(std::abs(slider->value - 100.f) < .01f, "dragging past the end clamps at max");

    silica::PointerState drop;
    drop.x        = 500.f;
    drop.y        = 28.f;
    drop.released = true;
    router2.Update(formRoot, drop);
    check(silica::FocusManager::Get().Current() == slider.get(), "dragging focuses the slider");
    check(slider->OnKeyDown(VK_LEFT) && slider->value < 100.f, "arrow keys step the slider");

    // Smooth value easing: programmatic and keyboard changes animate, while a
    // grabbed knob keeps tracking the pointer without lag.
    {
        silica::SilicaSlider smoothSlider;
        smoothSlider.width     = 200.f;
        smoothSlider.height    = 20.f;
        smoothSlider.min       = 0.f;
        smoothSlider.max       = 100.f;
        smoothSlider.SetValue(0.f, false);
        check(std::abs(smoothSlider.DisplayValue()) < .01f, "slider display follows an instant change");
        smoothSlider.smooth    = true;
        smoothSlider.smoothing = 22.f;
        smoothSlider.SetValue(80.f, false);
        check(std::abs(smoothSlider.value - 80.f) < .01f, "smooth slider still stores the target");
        check(std::abs(smoothSlider.DisplayValue()) < .01f, "smooth slider eases instead of jumping");
        for (int i = 0; i < 300; ++i) smoothSlider.Update();
        check(std::abs(smoothSlider.DisplayValue() - 80.f) < .05f, "smooth slider settles on its target");
        smoothSlider.pressed = true;
        smoothSlider.SetValue(20.f, false);
        smoothSlider.Update();
        check(
            smoothSlider.DisplayValue() > 20.01f && smoothSlider.DisplayValue() < 79.99f,
            "a grabbed knob glides toward the pointer"
        );
        smoothSlider.pressed = false;
        for (int i = 0; i < 300; ++i) smoothSlider.Update();
        check(std::abs(smoothSlider.DisplayValue() - 20.f) < .05f, "the knob keeps easing after release");
    }

    // TextBox: click to focus, type characters, enforce max_length, backspace,
    // submit and save on blur.
    clickAt(50.f, 49.f);
    check(silica::FocusManager::Get().Current() == textBox.get(), "clicking focuses the text box");

    textBox->OnKeyDown('D');
    textBox->OnKeyDown('E');
    check(textBox->text == "abcde", "typing appends characters");
    textBox->OnKeyDown('F');
    check(textBox->text == "abcde", "max_length rejects further characters");
    check(lastText == "abcde", "text box reports every edit");

    textBox->OnKeyDown(VK_BACK);
    check(textBox->text == "abcd", "backspace removes one character");

    textBox->OnKeyDown(VK_RETURN);
    check(submits == 1, "Enter submits the text box");

    silica::FocusManager::Get().Clear();
    check(lastText == "abcd", "losing focus saves the final text");

    silica::FocusManager::Get().Set(textBox.get());
    check(textBox->OnKeyDown(VK_ESCAPE), "Esc is consumed while editing");
    check(silica::FocusManager::Get().Current() == nullptr, "Esc drops the text box focus");

    // Dropdown: open, hit the expanded list, select, close and report.
    clickAt(80.f, 70.f);
    check(dropdown->IsOpen(), "clicking opens the dropdown");
    check(
        silica::HitTest(formRoot, 80.f, 98.f) == dropdown,
        "the expanded list extends the dropdown hit area"
    );

    clickAt(80.f, 133.f); // third row
    check(!dropdown->IsOpen() && dropdown->selected == 2, "clicking an option selects and collapses");
    check(lastMode == 2, "dropdown reports the selected index");
    check(dropdown->SelectedText() == "冒险", "dropdown returns the selected text");

    clickAt(80.f, 70.f);
    check(dropdown->IsOpen(), "the dropdown can be opened again");
    clickAt(10.f, 6.f);
    check(!dropdown->IsOpen(), "focus moving elsewhere closes the dropdown");

    check(
        silica::FocusManager::Get().Current() == checkbox.get(),
        "the outside click focuses the checkbox and drops the dropdown"
    );

    // CSS layer: linear-gradient syntax.
    {
        auto gradientRoot = silica::UICompiler::Compile(R"(
            Panel {
                name: gradient_panel;
                width: 120;
                height: 40;
                background: linear-gradient(to right, #498FC3, #6BA9D9);
            }
        )");
        check(gradientRoot != nullptr, "gradient document compiles");
        auto const panel = std::dynamic_pointer_cast<silica::SilicaPanel>(gradientRoot);
        check(panel && panel->style.gradient.has_value(), "linear-gradient fills the style gradient");
        if (panel && panel->style.gradient) {
            auto const& gradient = *panel->style.gradient;
            check(gradient.horizontal, "'to right' picks the horizontal axis");
            check(gradient.stops.size() == 2, "both color stops are parsed");
            check(std::abs(gradient.from.r - 0x49 / 255.f) < .01f, "the first stop is #498FC3");
            check(std::abs(gradient.to.b - 0xD9 / 255.f) < .01f, "the last stop is #6BA9D9");
        }
    }

    // CSS layer: transition on a state-driven value.
    {
        auto transitionRoot = silica::UICompiler::Compile(R"(
            Checkbox {
                name: animated_check;
                width: 120;
                height: 18;
                checked: false;
                background: @checked ? "#6CC349" : "#33363C";
                transition: background 0.4s linear;
            }
        )");
        auto const animated = std::dynamic_pointer_cast<silica::SilicaCheckbox>(transitionRoot);
        check(animated != nullptr, "conditional style document compiles");
        if (animated) {
            check(std::abs(animated->style.background.g - 0x36 / 255.f) < .01f, "unchecked background applies");
            animated->checked = true;
            silica::TickTree(*transitionRoot, 0.2f);
            float const mid = animated->style.background.g;
            check(mid > 0.44f && mid < 0.54f, "halfway through the transition the color is interpolated");
            silica::TickTree(*transitionRoot, 0.3f);
            check(
                std::abs(animated->style.background.g - 0xC3 / 255.f) < .01f
                    && std::abs(animated->style.background.b - 0x49 / 255.f) < .01f,
                "the transition settles on the checked color"
            );
        }
    }

    // CSS layer: @keyframes with opacity and scale applied per Tick.
    {
        auto keyframeRoot = silica::UICompiler::Compile(R"(
            @keyframes pulse {
                0% { opacity: 1; transform: scale(1); }
                50% { opacity: 0.6; transform: scale(0.95); }
                100% { opacity: 1; transform: scale(1); }
            }
            Button {
                name: pulse_button;
                text: "pulse";
                width: 80;
                height: 24;
                animation: pulse 1s infinite;
            }
        )");
        auto const pulse = std::dynamic_pointer_cast<silica::SilicaButton>(keyframeRoot);
        check(pulse != nullptr, "keyframe document compiles");
        if (pulse) {
            for (int i = 0; i < 20; ++i) silica::TickTree(*keyframeRoot, .025f);
            check(std::abs(pulse->style.opacity - 0.6f) < .02f, "opacity reaches the 50% keyframe");
            check(std::abs(pulse->AnimationScale() - 0.95f) < .02f, "scale reaches the 50% keyframe");
            for (int i = 0; i < 10; ++i) silica::TickTree(*keyframeRoot, .025f);
            check(std::abs(pulse->style.opacity - 0.8f) < .03f, "opacity interpolates inside a segment");
        }
    }

    // Reactive layer: DataStore bindings, expressions and click scripts.
    {
        silica::DataStore::Clear();
        silica::DataStore::Set("health", 0);
        silica::DataStore::Set("theme_color", std::string("#5CD65C"));

        auto reactiveRoot = silica::UICompiler::Compile(R"(
            Container {
                Label { name: hp_text; text: @health > 0 ? "存活" : "死亡"; color: @theme_color; }
                Label { name: hp_count; text: @health; visible: @health > 0; }
                Button { name: heal; text: "加血"; onClick: { @health = @health + 10; } }
            }
        )");
        check(reactiveRoot != nullptr, "reactive document compiles");

        auto hpText  = std::dynamic_pointer_cast<silica::SilicaLabel>(silica::FindByName(reactiveRoot, "hp_text"));
        auto hpCount = std::dynamic_pointer_cast<silica::SilicaLabel>(silica::FindByName(reactiveRoot, "hp_count"));
        auto heal    = std::dynamic_pointer_cast<silica::SilicaButton>(silica::FindByName(reactiveRoot, "heal"));

        check(hpText && hpText->text == "死亡", "the false branch of a ternary binding applies");
        check(
            hpText && std::abs(hpText->style.textColor.r - 0x5C / 255.f) < .01f
                && std::abs(hpText->style.textColor.g - 0xD6 / 255.f) < .01f,
            "a string data key feeds a color property"
        );
        check(hpCount && !hpCount->visible, "a relational binding hides the widget at zero");

        if (heal) heal->Click();
        check(silica::DataStore::Get("health").ToNumber() == 10.0, "a click script writes the DataStore");
        check(hpText && hpText->text == "存活", "the text binding updates from the store notification");
        check(hpCount && hpCount->visible && hpCount->text == "10", "a number binding formats as text");

        silica::DataStore::Set("health", -3);
        check(hpText && hpText->text == "死亡", "a store write flips the ternary");
        check(hpCount && !hpCount->visible, "a store write updates visible");

        silica::DataStore::Set("theme_color", std::string("#E05252"));
        check(
            hpText && std::abs(hpText->style.textColor.r - 0xE0 / 255.f) < .01f,
            "a color binding follows the store"
        );

        // Widget state is not stored in DataStore, so expressions that read it
        // are re-evaluated by Tick instead.
        auto stateRoot = silica::UICompiler::Compile(R"(
            Checkbox {
                name: state_check;
                checked: false;
                color: @checked ? "#E8ECEF" : "#87919A";
            }
        )");
        auto stateCheck =
            std::dynamic_pointer_cast<silica::SilicaCheckbox>(silica::FindByName(stateRoot, "state_check"));
        check(stateCheck != nullptr, "a state binding compiles");
        if (stateCheck) {
            check(
                std::abs(stateCheck->style.textColor.r - 0x87 / 255.f) < .01f,
                "the unchecked state picks the dim color"
            );
            stateCheck->checked = true;
            silica::TickTree(*stateRoot, 0.016f);
            check(
                std::abs(stateCheck->style.textColor.r - 0xE8 / 255.f) < .01f,
                "Tick re-evaluates bindings that read widget state"
            );
        }

        silica::DataStore::Clear();
    }

    // Stage 2: event payloads, if/else scripts, locals and two-way bindings.
    {
        silica::DataStore::Clear();
        silica::DataStore::Set("opt", true);
        silica::DataStore::Set("vol", 20.0);
        silica::DataStore::Set("toggles", 0.0);
        silica::DataStore::Set("mode", 1.0);
        silica::DataStore::Set("health2", 0.0);

        auto eventRoot = silica::UICompiler::Compile(R"(
            Container {
                Checkbox {
                    name: opt_box;
                    text: "x";
                    width: 100;
                    height: 18;
                    checked: @opt;
                    onChange: { @toggles += 1; @last = event.checked ? "on" : "off"; }
                }
                Slider {
                    name: vol_slider;
                    width: 100;
                    height: 20;
                    min: 0;
                    max: 100;
                    step: 10;
                    value: @vol;
                    onChange: { @vol_seen = event.value; }
                }
                TextBox {
                    name: name_box;
                    text: "";
                    width: 100;
                    height: 22;
                    onSubmit: { @submitted = event.text; }
                }
                Dropdown {
                    name: mode_box;
                    width: 120;
                    height: 24;
                    options: "生存, 创造, 冒险";
                    selected: @mode;
                    onChange: { @mode_text = event.text; @mode_index = event.index; }
                }
                Button {
                    name: clamp_button;
                    text: "clamp";
                    width: 60;
                    height: 20;
                    onClick: { @health2 += 25; if (@health2 > 60) { @health2 = 60; } }
                }
                Button {
                    name: local_button;
                    text: "local";
                    width: 60;
                    height: 20;
                    onClick: { $step = @health2 * 0.5; @health2 = @health2 + $step; }
                }
            }
        )");
        check(eventRoot != nullptr, "event script document compiles");

        auto optBox   = std::dynamic_pointer_cast<silica::SilicaCheckbox>(silica::FindByName(eventRoot, "opt_box"));
        auto volBar   = std::dynamic_pointer_cast<silica::SilicaSlider>(silica::FindByName(eventRoot, "vol_slider"));
        auto nameBox  = std::dynamic_pointer_cast<silica::SilicaTextBox>(silica::FindByName(eventRoot, "name_box"));
        auto modeBox  = std::dynamic_pointer_cast<silica::SilicaDropdown>(silica::FindByName(eventRoot, "mode_box"));
        auto clampBtn = std::dynamic_pointer_cast<silica::SilicaButton>(silica::FindByName(eventRoot, "clamp_button"));
        auto localBtn = std::dynamic_pointer_cast<silica::SilicaButton>(silica::FindByName(eventRoot, "local_button"));
        check(optBox && volBar && nameBox && modeBox && clampBtn && localBtn, "event demo widgets are findable");
        if (!optBox || !volBar || !nameBox || !modeBox || !clampBtn || !localBtn) {
            std::printf("FILE FALLBACK TEST FAIL\n");
            return 1;
        }

        check(optBox->checked, "a store value feeds the checkbox");
        optBox->OnActivate();
        check(!optBox->checked, "the user toggle wins locally");
        check(!silica::DataStore::Get("opt").Truthy(), "the toggle writes back to the store");
        check(silica::DataStore::Get("toggles").ToNumber() == 1.0, "compound assignment reads the store");
        check(silica::DataStore::Get("last").ToString() == "off", "the script reads event.checked");

        check(std::abs(volBar->value - 20.f) < .01f, "a store value feeds the slider");
        volBar->OnMouseDown(volBar->x + volBar->width, volBar->y + 1.f);
        check(std::abs(volBar->value - 100.f) < .01f, "dragging still edits the slider");
        check(silica::DataStore::Get("vol").ToNumber() == 100.0, "the drag writes back to the store");
        check(silica::DataStore::Get("vol_seen").ToNumber() == 100.0, "the slider script reads event.value");
        volBar->OnMouseUp(volBar->x + volBar->width, volBar->y + 1.f, false);

        check(modeBox->selected == 1, "a store value feeds the dropdown");
        modeBox->Select(2);
        check(silica::DataStore::Get("mode").ToNumber() == 2.0, "the dropdown writes its index back");
        check(silica::DataStore::Get("mode_text").ToString() == "冒险", "the dropdown script reads event.text");
        check(silica::DataStore::Get("mode_index").ToNumber() == 2.0, "the dropdown script reads event.index");

        silica::FocusManager::Get().Set(nameBox.get());
        nameBox->OnKeyDown('H');
        nameBox->OnKeyDown('I');
        nameBox->OnKeyDown(VK_RETURN);
        check(silica::DataStore::Get("submitted").ToString() == "hi", "the submit script reads event.text");
        silica::FocusManager::Get().Clear();

        clampBtn->Click();
        check(silica::DataStore::Get("health2").ToNumber() == 25.0, "compound assignment adds");
        clampBtn->Click();
        clampBtn->Click();
        check(silica::DataStore::Get("health2").ToNumber() == 60.0, "if/else clamps the value");

        localBtn->Click();
        check(silica::DataStore::Get("health2").ToNumber() == 90.0, "$locals scope to one script run");
        check(!silica::DataStore::Has("step"), "$locals never touch the store");

        silica::DataStore::Clear();
    }

    // Stage 3: array values, ForEach scoping, interpolation and rebuilds.
    {
        silica::DataStore::Clear();

        auto const items = silica::Variant::Array({
            silica::Variant::Object({{"name", "甲"}, {"count", 3.0}, {"id", 10.0}}),
            silica::Variant::Object({{"name", "乙"}, {"count", 5.0}, {"id", 20.0}}),
        });
        check(items.IsArray() && items.Count() == 2, "Variant stores an array");
        check(items.Index(1).Field("name").ToString() == "乙", "Variant reads an object field");
        check(
            items
                == silica::Variant::Array(
                    {
                        silica::Variant::Object({{"name", "甲"}, {"count", 3.0}, {"id", 10.0}}),
                        silica::Variant::Object({{"name", "乙"}, {"count", 5.0}, {"id", 20.0}}),
                    }
                ),
            "array equality compares elements deeply"
        );

        silica::DataStore::Set("hits", items);
        silica::DataStore::Set("selected_item", 0.0);

        auto listRoot = silica::UICompiler::Compile(R"(
            Container {
                layout: vertical;
                width: 200;
                height: 100;

                ForEach (data: @hits, item: @item, index: @idx) {
                    name: hits_list;
                    Button {
                        name: row_button;
                        text: "{{@item.name}} - {{@item.count}}";
                        width: 160;
                        height: 20;
                        background: @idx % 2 == 0 ? "#222222" : "#333333";
                        onClick: { @selected_item = @item.id; }
                    }
                }
            }
        )");
        check(listRoot != nullptr, "ForEach document compiles");

        auto forEach = std::dynamic_pointer_cast<silica::SilicaContainer>(
            silica::FindByName(listRoot, "hits_list")
        );
        if (!forEach) {
            // The name is optional on the container; fall back to the only child.
            forEach = std::dynamic_pointer_cast<silica::SilicaContainer>(listRoot);
        }
        check(forEach && forEach->Count() == 2, "ForEach builds one row per element");
        if (!forEach || forEach->Count() != 2) {
            std::printf("FILE FALLBACK TEST FAIL\n");
            return 1;
        }

        auto first  = std::dynamic_pointer_cast<silica::SilicaButton>(forEach->Children()[0]);
        auto second = std::dynamic_pointer_cast<silica::SilicaButton>(forEach->Children()[1]);
        check(first && second, "each row is the template button");
        if (!first || !second) {
            std::printf("FILE FALLBACK TEST FAIL\n");
            return 1;
        }

        check(first->text == "甲 - 3", "interpolation reads item fields and index order");
        check(second->text == "乙 - 5", "every row sees its own element");
        check(
            std::abs(first->style.background.r - 0x22 / 255.f) < .01f,
            "the even index picks the first zebra color"
        );
        check(
            std::abs(second->style.background.r - 0x33 / 255.f) < .01f,
            "the modulo operator picks the second zebra color"
        );

        first->Click();
        check(
            silica::DataStore::Get("selected_item").ToNumber() == 10.0,
            "a row script writes its item id into the store"
        );

        silica::DataStore::Set(
            "hits",
            silica::Variant::Array({
                silica::Variant::Object({{"name", "丙"}, {"count", 7.0}, {"id", 30.0}}),
                silica::Variant::Object({{"name", "丁"}, {"count", 1.0}, {"id", 40.0}}),
                silica::Variant::Object({{"name", "戊"}, {"count", 2.0}, {"id", 50.0}}),
            })
        );
        listRoot->Update();
        check(forEach->Count() == 3, "a new array rebuilds the row set");
        auto third = forEach->Count() == 3
                         ? std::dynamic_pointer_cast<silica::SilicaButton>(forEach->Children()[2])
                         : nullptr;
        check(third && third->text == "戊 - 2", "rebuilt rows read the new array");

        silica::DataStore::Set("hits", silica::Variant::Array({}));
        listRoot->Update();
        check(forEach->Count() == 0, "an empty array clears every row");

        silica::DataStore::Clear();
    }

    // Declarative reuse: variables, styles with extends, components with
    // parameters and Slot, wrapped labels and row caps.
    {
        silica::DataStore::Clear();
        silica::DataStore::Set("health", 5.0);

        auto reuseRoot = silica::UICompiler::Compile(R"(
            Var accent: #5CD65C;
            Var gutter: 6;

            Style Card {
                background: #1E2429;
                padding: $gutter;
                border: 1 solid #30363C;
            }
            Style Glow extends Card { background: #263238; }

            Component Stat(title: "属性", value: 0) {
                Label { name: stat_title; text: "@title"; color: $accent; }
                Label { name: stat_value; text: @value; }
                Slot { }
            }

            Container {
                name: reuse;
                Stat(title: "生命", value: @health) {
                    Button { name: stat_button; text: "+"; width: 20; height: 18; }
                }
                Stat { name: stat_default; }
                Panel { name: card_panel; class: "glow"; background: #101010; }
                Label { name: wrapped; text: "long text"; wrap: true; }
            }
        )");
        check(reuseRoot != nullptr, "variables, styles and components compile");
        if (reuseRoot) reuseRoot->Update();

        auto statTitle = std::dynamic_pointer_cast<silica::SilicaLabel>(silica::FindByName(reuseRoot, "stat_title"));
        auto statValue = std::dynamic_pointer_cast<silica::SilicaLabel>(silica::FindByName(reuseRoot, "stat_value"));
        auto statButton = silica::FindByName(reuseRoot, "stat_button");
        check(
            statTitle && statTitle->text == "生命" && std::abs(statTitle->style.textColor.g - 0xD6 / 255.f) < .01f,
            "a literal argument and a $variable style reach the component body"
        );
        check(statValue && statValue->text == "5", "a reactive argument binds in the component body");
        check(statButton != nullptr, "Slot content is inserted into the component body");

        silica::DataStore::Set("health", 7.0);
        check(statValue && statValue->text == "7", "a store write updates the component's bound value");

        auto statDefault = std::dynamic_pointer_cast<silica::SilicaContainer>(silica::FindByName(reuseRoot, "stat_default"));
        check(statDefault && statDefault->Count() == 2, "a component without Slot children keeps its own body");
        if (statDefault && statDefault->Count() == 2) {
            auto titleProbe  = std::dynamic_pointer_cast<silica::SilicaLabel>(statDefault->Children()[0]);
            auto valueProbe  = std::dynamic_pointer_cast<silica::SilicaLabel>(statDefault->Children()[1]);
            check(titleProbe && titleProbe->text == "属性", "a default argument supplies the title");
            check(valueProbe && valueProbe->text == "0", "a numeric default supplies the value");
        }

        auto cardPanel = silica::FindByName(reuseRoot, "card_panel");
        check(
            cardPanel && std::abs(cardPanel->style.background.r - 0x10 / 255.f) < .01f,
            "an inline property beats the class background"
        );
        check(
            cardPanel && std::abs(cardPanel->padding - 6.f) < .01f && cardPanel->style.borderWidth > 0.f,
            "the style chain contributes padding and border"
        );

        auto wrapped = std::dynamic_pointer_cast<silica::SilicaLabel>(silica::FindByName(reuseRoot, "wrapped"));
        check(
            wrapped && wrapped->wrap && std::abs(wrapped->height - 30.f) < .01f,
            "wrap: true defaults a label to three lines"
        );

        silica::DataStore::Clear();
    }

    // Recursive components and style cycles are reported, not crashed on.
    {
        auto recursiveRoot = silica::UICompiler::CompileDetailed(R"(
            Component Loop { Loop { } }
            Container { Loop { } }
        )");
        bool recursionReported = false;
        for (auto const& diagnostic : recursiveRoot.diagnostics) {
            if (diagnostic.message.find("recursively") != std::string::npos) recursionReported = true;
        }
        check(recursiveRoot.root != nullptr && recursionReported, "a recursive component yields a diagnostic");

        auto styleCycle = silica::UICompiler::CompileDetailed(R"(
            Style A extends B { background: #111111; }
            Style B extends A { padding: 2; }
            Container { class: "a"; }
        )");
        bool cycleReported = false;
        for (auto const& diagnostic : styleCycle.diagnostics) {
            if (diagnostic.message.find("extends itself") != std::string::npos) cycleReported = true;
        }
        check(styleCycle.root != nullptr && cycleReported, "a style cycle yields a diagnostic");
    }

    // A runaway array is truncated to the row cap, and non-finite maths folds
    // back to zero instead of poisoning layout or the store.
    {
        silica::DataStore::Clear();

        std::vector<silica::Variant> many;
        many.reserve(600);
        for (int i = 0; i < 600; ++i) many.emplace_back(static_cast<double>(i));
        silica::DataStore::Set("many", silica::Variant::Array(std::move(many)));
        silica::DataStore::Set("huge", 1e300);

        auto guardRoot = silica::UICompiler::Compile(R"(
            Container {
                ForEach (data: @many, item: @item) {
                    name: many_list;
                    Label { text: "{{@item}}"; }
                }
                Label { name: huge_label; text: @huge * @huge; }
                Button { name: huge_button; text: "x"; onClick: { @huge = @huge * @huge; } }
            }
        )");
        check(guardRoot != nullptr, "guard document compiles");

        auto manyList = std::dynamic_pointer_cast<silica::SilicaContainer>(silica::FindByName(guardRoot, "many_list"));
        if (!manyList) manyList = std::dynamic_pointer_cast<silica::SilicaContainer>(guardRoot);
        check(manyList && manyList->Count() == 512, "ForEach caps the row count");

        auto hugeLabel = std::dynamic_pointer_cast<silica::SilicaLabel>(silica::FindByName(guardRoot, "huge_label"));
        check(hugeLabel && hugeLabel->text == "0", "a non-finite binding result becomes zero");

        auto hugeButton = std::dynamic_pointer_cast<silica::SilicaButton>(silica::FindByName(guardRoot, "huge_button"));
        if (hugeButton) hugeButton->Click();
        check(silica::DataStore::Get("huge").ToNumber() == 0.0, "a non-finite script result is stored as zero");

        silica::DataStore::Clear();
    }

    std::printf(g_failures == 0 ? "FILE FALLBACK TEST PASS\n" : "FILE FALLBACK TEST FAIL\n");
    return g_failures == 0 ? 0 : 1;
}
