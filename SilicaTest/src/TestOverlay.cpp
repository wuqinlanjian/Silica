// Silica UI test harness.
//
// The panel is a .sil document (ui/panel.sil). To get a real cursor and focus
// for a mod-drawn UI, P pushes an empty native dialog scene onto the client
// scene stack, cancels that scene's view render and draws the Silica tree in
// its place. Hover comes from the view's pointer (GUI units), clicks from the
// mouse input event (window pixels scaled by GuiData::mInvGuiScale). No Win32
// cursor polling and no game hooks are involved.

#include "TestOverlay.h"

#include <windows.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "silica/Animation.h"
#include "silica/Checkbox.h"
#include "silica/DataStore.h"
#include "silica/ImagePack.h"
#include "silica/Silica.h"
#include "silica/ScrollView.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Symbol.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/input/HIDController.h"
#include "mc/deps/input/MouseAction.h"

namespace silica_test {
namespace {

using namespace silica;

std::mutex    g_logMutex;
std::ofstream g_log;

constexpr char kDocument[] = "ui/panel.sil";

// The library swaps the tree when it is reloaded, so everything below is
// guarded by one recursive lock: button callbacks can fire from inside the
// render pass and re-enter these helpers.
std::recursive_mutex g_mutex;
bool                 g_open = false;
std::atomic<bool>    g_gameplay{false};

WidgetPtr                    g_root;
std::shared_ptr<SilicaLabel> g_hint;
std::shared_ptr<SilicaLabel> g_status;
std::shared_ptr<SilicaLabel> g_volumeValue;
std::shared_ptr<SilicaScrollView> g_scroller;

FocusRing     g_focus;
PointerRouter g_pointer;
int           g_theme = 0;

constexpr float kThemeOpacity[] = {1.f, .78f, .55f};

// The scene that owns input while the panel is open. Its view is identified by
// name; views that were already on screen before opening are never mistaken
// for it.
std::shared_ptr<AbstractScene>        g_shell;
ScreenView*                           g_shellView    = nullptr;
bool                                  g_shellTopSeen = false;
std::unordered_set<std::string>       g_knownRoots;

float g_invScale = 1.f; // pixels -> GUI units, refreshed during render

struct MouseEdge {
    bool  down;
    float x;
    float y;
};
std::vector<MouseEdge> g_mouseEdges;

// Wheel events are queued like button edges and delivered on the render
// thread; the notch amount and the pointer position both come from the event.
struct WheelEdge {
    float x;
    float y;
    float delta;
};
std::vector<WheelEdge> g_wheelEdges;

// Last hover position in GUI units, used when a wheel event arrives without a
// usable position.
float g_pointerX = 0.f;
float g_pointerY = 0.f;

std::string normalizeName(std::string_view text) {
    std::string out;
    for (char c : text) {
        auto const lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) out.push_back(lower);
    }
    return out;
}

bool namesMatch(std::string const& viewRoot, std::string const& sceneName) {
    auto const view  = normalizeName(viewRoot);
    auto const scene = normalizeName(sceneName);
    if (view.empty() || scene.empty()) return false;
    return view == scene || view.find(scene) != std::string::npos || scene.find(view) != std::string::npos;
}

ISceneStack* currentStack(ClientInstance& client) { return &*client.getCurrentSceneStack(); }

float invGuiScale(ClientInstance& client) {
    GuiData* data = &*client.getGuiData();
    return data ? data->mInvGuiScale : 1.f;
}

// The SceneFactory method is a plain game function that the local LeviLamina
// import library does not export, so it is resolved by name at runtime. It must
// be called through a real member pointer: the MS ABI keeps `this` in RCX and
// the shared_ptr return slot in RDX, which a free-function signature gets
// backwards.
using CreateDialogScene =
    std::shared_ptr<AbstractScene> (SceneFactory::*)(std::string const&, std::string const&);

CreateDialogScene createDialogSceneFn() {
    static CreateDialogScene const fn = [] {
        void* address = ll::memory::SymbolView(
                            "?createCommonDialogInfoScreen@SceneFactory@@QEAA?AV?$shared_ptr@VAbstractScene@@@std@@AEBV?$"
                            "basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@3@0@Z"
        ).resolve(true);
        CreateDialogScene result{};
        if (address) {
            static_assert(sizeof(result) == sizeof(address));
            std::memcpy(&result, &address, sizeof(address));
        }
        return result;
    }();
    return fn;
}

std::shared_ptr<SilicaLabel> labelNamed(char const* name) {
    return std::dynamic_pointer_cast<SilicaLabel>(FindByName(g_root, name));
}

void bindNames() {
    g_hint        = labelNamed("hint");
    g_status      = labelNamed("status");
    g_volumeValue = labelNamed("volume_value");
    g_scroller    = std::dynamic_pointer_cast<SilicaScrollView>(FindByName(g_root, "scroll_area"));
    logLine(
        "[ui] panel bound: hint=%s status=%s scroller=%s",
        g_hint ? "yes" : "none",
        g_status ? "yes" : "none",
        g_scroller ? "yes" : "none"
    );
}

void cycleTheme() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_root) return;

    g_theme = (g_theme + 1) % 3;
    float const opacity = kThemeOpacity[g_theme];
    g_root->style.opacity = opacity;
    if (g_status) {
        char text[64];
        std::snprintf(text, sizeof(text), "主题：面板不透明度 %d%%", static_cast<int>(opacity * 100.f + .5f));
        g_status->text = text;
    }
}

void toggleHint() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_hint) return;
    g_hint->visible = !g_hint->visible;
    if (g_status) g_status->text = g_hint->visible ? "提示文本：显示" : "提示文本：隐藏";
}

void setStatus(std::string text) {
    if (g_status) g_status->text = std::move(text);
}

// The sidebar items are checkboxes used as toggles; only one may stay on, so
// every click re-applies the full selection without notifying back.
void selectNav(std::string_view name) {
    static constexpr char const* kNavItems[] = {
        "nav_general",
        "nav_quality",
        "nav_performance",
        "nav_iris_settings",
        "nav_iris_shaders",
        "nav_vitrail_shaders",
    };
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    for (char const* item : kNavItems) {
        auto box = std::dynamic_pointer_cast<SilicaCheckbox>(FindByName(g_root, item));
        if (box) box->checked = name == item;
    }
}

void resetPanelState() {
    // Hover/pressed flags live on the widgets; clear them while the tree is
    // still alive.
    g_pointer.Reset();
    silica::FocusManager::Get().Clear();
    g_open         = false;
    g_shellView    = nullptr;
    g_shellTopSeen = false;
    g_mouseEdges.clear();
    g_wheelEdges.clear();
    g_focus.Collect(nullptr);
}

// Pops the shell when it is still the top scene; the frame path also calls this
// when the game closed the dialog on its own.
void closePanel(bool pop, char const* reason) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_open) return;

    std::shared_ptr<AbstractScene> scene = std::move(g_shell);
    g_shell.reset();
    resetPanelState();

    if (pop && scene) {
        if (auto maybe = ll::service::getClientInstance()) {
            ISceneStack* stack = currentStack(*maybe);
            if (stack && stack->getTopScene() == scene.get()) stack->schedulePopScreen(1);
        }
    }
    logLine("[ui] closed (%s)", reason);
}

void openPanel() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_open || !g_root) return;

    auto maybe = ll::service::getClientInstance();
    if (!maybe) {
        logLine("[ui] no client instance; panel not opened");
        return;
    }
    ClientInstance& client = *maybe;
    ISceneStack*    stack  = currentStack(client);
    if (!stack) {
        logLine("[ui] no scene stack; panel not opened");
        return;
    }

    std::shared_ptr<AbstractScene> scene;
    CreateDialogScene const      createScene = createDialogSceneFn();
    if (!createScene) {
        logLine("[ui] SceneFactory::createCommonDialogInfoScreen was not resolved");
        return;
    }
    try {
        scene = (client.getSceneFactory().*createScene)(std::string("Silica"), std::string());
    } catch (std::exception const& e) {
        logLine("[ui] native screen creation threw: %s", e.what());
        return;
    } catch (...) {
        logLine("[ui] native screen creation threw (non-standard exception)");
        return;
    }
    if (!scene) {
        logLine("[ui] native screen creation returned nothing");
        return;
    }

    stack->pushScreen(scene, false);
    g_shell        = std::move(scene);
    g_shellView    = nullptr;
    g_shellTopSeen = false;
    g_open         = true;
    g_mouseEdges.clear();
    g_wheelEdges.clear();
    g_pointer.Reset();
    g_focus.Collect(g_root);
    if (g_root->style.opacity != kThemeOpacity[g_theme]) {
        g_root->style.opacity = kThemeOpacity[g_theme];
    }
    logLine("[ui] opened (native screen, %zu focusable(s))", g_focus.Size());
}

// Initial values for the reactive demo in panel.sil. The Has guard keeps an F5
// reload from resetting state the click script already changed.
void seedDemoData() {
    if (!silica::DataStore::Has("health")) silica::DataStore::Set("health", 100);
    if (!silica::DataStore::Has("theme_color")) {
        silica::DataStore::Set("theme_color", std::string("#5CD65C"));
    }
    if (!silica::DataStore::Has("demo_on")) silica::DataStore::Set("demo_on", true);
    if (!silica::DataStore::Has("demo_items")) {
        silica::DataStore::Set(
            "demo_items",
            silica::Variant::Array({
                silica::Variant::Object({{"id", 1.0}, {"name", "钻石"}, {"count", 12.0}}),
                silica::Variant::Object({{"id", 2.0}, {"name", "铁锭"}, {"count", 64.0}}),
                silica::Variant::Object({{"id", 3.0}, {"name", "苹果"}, {"count", 5.0}}),
            })
        );
    }
    if (!silica::DataStore::Has("selected_item")) silica::DataStore::Set("selected_item", 1.0);
    if (!silica::DataStore::Has("demo_locked")) silica::DataStore::Set("demo_locked", false);
}

void reloadPanel() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    seedDemoData();
    auto reloaded = Silica::ReloadUI(kDocument);    if (!reloaded) {
        logLine("[ui] reload returned no tree");
        return;
    }

    g_pointer.Reset();
    g_focus.Collect(nullptr);
    g_mouseEdges.clear();
    g_wheelEdges.clear();
    g_root = std::move(reloaded);
    if (g_root->width <= 0.f) g_root->width = 420.f;
    if (g_root->height <= 0.f) g_root->height = 260.f;
    silica::imagepack::Warmup(g_root);
    bindNames();
    g_root->style.opacity = kThemeOpacity[g_theme];
    g_root->Update();
    g_focus.Collect(g_root);
    logLine("[ui] reloaded %s (%zu focusable(s))", kDocument, g_focus.Size());
}

// Embedded fallback for Silica::CompileFile; it keeps the names the harness
// binds to, so a missing or broken document still yields a usable panel.
constexpr char kEmbeddedSil[] = R"(
Container {
    name: test_panel;
    layout: vertical;
    width: 420;
    height: 260;
    padding: 12;
    background: #0E0F11EB;
    border: 1 solid #5A5B5CE6;

    Label {
        name: title;
        text: "Silica 测试（内置回退）";
        font-size: 1.4;
        color: #F2F4F6;
        margin-bottom: 2;
    }

    Label {
        name: subtitle;
        text: "ui/panel.sil 缺失或无法编译";
        font-size: 0.8;
        color: #B4B8B6;
        margin-bottom: 10;
    }

    Container {
        name: button_row;
        layout: horizontal;
        width: 0;
        height: 24;
        margin-bottom: 10;

        Button {
            name: theme_button;
            text: "切换主题";
            width: 104;
            height: 24;
            margin-right: 8;
            background: #6CC349;
            border: 1 solid #8CD96B;
            onClick: "cycle_theme";
        }

        Button {
            name: hint_button;
            text: "显示/隐藏提示";
            width: 116;
            height: 24;
            background: #6CC349;
            border: 1 solid #8CD96B;
            onClick: "toggle_hint";
        }
    }

    Label {
        name: hint;
        text: "提示文本：第二个按钮切换我的 visible。";
        font-size: 0.9;
        color: #F2F4F6;
        margin-bottom: 6;
    }

    Label {
        name: status;
        text: "就绪";
        font-size: 0.85;
        color: #F2F4F6;
    }
}
)";

size_t countWidgets(WidgetPtr const& widget) {
    if (!widget) return 0;
    size_t count = 1;
    if (auto container = std::dynamic_pointer_cast<SilicaContainer>(widget)) {
        for (auto const& child : container->Children()) count += countWidgets(child);
    }
    return count;
}

void logRoot(char const* label, char const* path, WidgetPtr const& root) {
    logLine(
        "[sil] %s: path=%s root=%s name=%s widgets=%zu",
        label,
        path,
        root ? "yes" : "none",
        root && !root->name.empty() ? root->name.c_str() : "-",
        countWidgets(root)
    );
}

// Exercises the production load path: the document comes from
// <modDir>/ui/panel.sil with the embedded fallback behind it, then the same
// interface is reloaded from disk to prove ReloadUI re-reads the file.
void loadPanel() {
    seedDemoData();
    WidgetPtr root = Silica::CompileFile(kDocument, kEmbeddedSil);
    logRoot("file", kDocument, root);

    WidgetPtr fallback = Silica::CompileFile("ui/missing.sil", kEmbeddedSil);
    logRoot("fallback", "ui/missing.sil", fallback);

    WidgetPtr reloaded = Silica::ReloadUI(kDocument);
    logRoot("reload", kDocument, reloaded);

    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_root = reloaded ? reloaded : root;
    if (!g_root) {
        logLine("[ui] no tree; the panel stays disabled");
        return;
    }
    if (g_root->width <= 0.f) g_root->width = 420.f;
    if (g_root->height <= 0.f) g_root->height = 260.f;
    // Pack the file-backed images before the engine builds its pack stack.
    silica::imagepack::Warmup(g_root);
    bindNames();
    g_root->Update();
}

bool hudView(ScreenView const& view) {
    auto const& tree = view.mVisualTree;
    return tree && std::string_view(*tree->mRootControlName).ends_with(".hud_screen");
}

double frameSeconds() {
    using clock                    = std::chrono::steady_clock;
    static clock::time_point const start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

void drawPanel(MinecraftUIRenderContext& context, ScreenView& view) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_open || !g_root) return;

    glm::vec2 const size = view.mSize;
    float const     w    = size.x;
    float const     h    = size.y;
    if (!(w > 60.f && h > 60.f)) return;

    FillRect(context, 0.f, 0.f, w, h, Color{0.f, 0.f, 0.f, .42f});

    if (g_root->width <= 0.f) g_root->width = 420.f;
    if (g_root->height <= 0.f) g_root->height = 260.f;
    g_root->x = std::floor((w - g_root->width) * .5f);
    g_root->y = std::floor((h - g_root->height) * .5f);

    // CSS-style animations advance with the frame clock, before layout and
    // hit testing see the tree.
    static double lastFrame = -1.0;
    double const  now       = frameSeconds();
    float const   deltaTime =
        lastFrame < 0.0 ? 0.f : static_cast<float>(std::min(now - lastFrame, .1));
    lastFrame = now;
    silica::TickTree(*g_root, deltaTime);

    g_root->Update();

    // Down/up edges are replayed before hover so a click that started and
    // finished inside one frame is not lost.
    for (auto const& edge : g_mouseEdges) {
        PointerState state;
        state.x        = edge.x;
        state.y        = edge.y;
        state.down     = edge.down;
        state.pressed  = edge.down;
        state.released = !edge.down;
        g_pointer.Update(g_root, state);
    }
    g_mouseEdges.clear();

    for (auto const& edge : g_wheelEdges) {
        PointerState state;
        state.x = edge.x;
        state.y = edge.y;
        g_pointer.Wheel(g_root, state, edge.delta);
    }
    g_wheelEdges.clear();

    glm::vec2 const pointer = view.mPointerLocationPrevious;
    g_pointerX              = pointer.x;
    g_pointerY              = pointer.y;
    PointerState hover;
    hover.x = pointer.x;
    hover.y = pointer.y;
    g_pointer.Update(g_root, hover);

    g_root->Render(context);
    // Fills and text are submitted lazily at ordering boundaries; the frame
    // ends here, so hand them to the game before other UI draws.
    silica::FlushDraws(context);
}

void onRender(ll::event::BeforeUIRenderEvent& event) {
    try {
        ScreenView& view = event.screenView();

        auto maybe = ll::service::getClientInstance();
        ClientInstance* client = maybe ? &*maybe : nullptr;
        if (client) {
            float const scale = invGuiScale(*client);
            if (scale > 0.f) {
                std::lock_guard<std::recursive_mutex> lock(g_mutex);
                g_invScale = scale;
            }
        }

        if (!g_open) {
            if (hudView(view)) g_gameplay.store(true, std::memory_order_relaxed);
            if (view.mVisualTree) {
                std::lock_guard<std::recursive_mutex> lock(g_mutex);
                g_knownRoots.insert(normalizeName(*view.mVisualTree->mRootControlName));
            }
            return;
        }

        {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            if (!g_shell) return;

            ISceneStack*  stack = client ? currentStack(*client) : nullptr;
            AbstractScene* top  = stack ? stack->getTopScene() : nullptr;
            if (top != g_shell.get()) {
                // The game closed the dialog by itself (Esc or a controller
                // path we did not intercept).
                if (g_shellTopSeen) closePanel(false, "native screen closed");
                return;
            }
            g_shellTopSeen = true;

            if (!g_shellView) {
                std::string const sceneName = top->getScreenName();
                std::string const rootName =
                    view.mVisualTree ? *view.mVisualTree->mRootControlName : std::string();
                // A view that was already on screen before opening belongs to
                // the gameplay stack; skip it.
                if (!namesMatch(rootName, sceneName) && g_knownRoots.count(normalizeName(rootName))) return;
                g_shellView = &view;
                logLine("[ui] shell view: root=%s scene=%s", rootName.c_str(), sceneName.c_str());
            } else if (&view != g_shellView) {
                return;
            }
        }

        event.cancel();
        drawPanel(event.uiRenderContext(), view);
    } catch (...) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            logLine("[ui] render threw; the panel is skipped for this frame");
        }
    }
}

void onKey(ll::event::input::KeyInputEvent& event) {
    int const  key  = event.keyCode();
    bool const down = event.isDown();

    if (!g_open) {
        if (down && key == 'P') {
            if (!g_gameplay.load(std::memory_order_relaxed)) return;
            if (event.controller().mTextboxIsFocused || event.controller().mTextboxIsSelected) return;
            event.cancel();
            openPanel();
        }
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_open) return;
    event.cancel();
    if (!down) return;

    // The focused control sees the key first: the text box edits text, the
    // dropdown collapses on Esc and the slider consumes its arrow keys. A key
    // the control consumes must not reach the harness shortcuts below.
    if (SilicaWidget* focused = silica::FocusManager::Get().Current()) {
        if (focused->OnKeyDown(key)) return;
    }

    switch (key) {
    case 'P':
        // Reachable only when no focused control took the character.
        closePanel(true, "P");
        break;
    case VK_ESCAPE:
        // Leave the stack walk to the next render; the shell is still top
        // here.
        closePanel(true, "Esc");
        break;
    case VK_F5:
        reloadPanel();
        break;
    case VK_UP:
    case VK_LEFT:
        g_focus.Move(-1);
        break;
    case VK_DOWN:
    case VK_RIGHT:
        g_focus.Move(1);
        break;
    case VK_RETURN:
    case VK_SPACE:
        g_focus.Activate();
        break;
    default:
        break;
    }
}

void onMouse(ll::event::input::MouseInputEvent& event) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_open) return;

    int const action = event.actionButtonId();
    if (action == MouseAction::ActionWheel) {
        // The notch amount lives in the button data; dy is a pixel fallback for
        // drivers that report the wheel as motion.
        float const scale = g_invScale > 0.f ? g_invScale : 1.f;
        float       delta = static_cast<float>(event.buttonData());
        if (delta == 0.f) delta = static_cast<float>(event.dy()) / 120.f;
        if (delta != 0.f) {
            float x = static_cast<float>(event.x()) * scale;
            float y = static_cast<float>(event.y()) * scale;
            if (x == 0.f && y == 0.f) {
                x = g_pointerX;
                y = g_pointerY;
            }
            g_wheelEdges.push_back({x, y, delta});
            static bool logged = false;
            if (!logged) {
                logged = true;
                logLine(
                    "[ui] wheel: data=%d dy=%d dx=%d -> delta=%.2f at %.1f,%.1f",
                    static_cast<int>(event.buttonData()),
                    static_cast<int>(event.dy()),
                    static_cast<int>(event.dx()),
                    delta,
                    x,
                    y
                );
            }
        }
        event.cancel();
        return;
    }

    float const scale = g_invScale > 0.f ? g_invScale : 1.f;

    if (action == MouseAction::ActionMove || action == MouseAction::ActionMoveRelative) {
        // The native screen tracks the pointer itself; cancelling moves would
        // freeze the cursor.
        return;
    }

    bool const isButton = action == MouseAction::ActionLeft || action == MouseAction::ActionRight
                       || action == MouseAction::ActionMiddle || action == MouseAction::ActionX1
                       || action == MouseAction::ActionX2;
    if (!isButton) return;

    float const x = static_cast<float>(event.x()) * scale;
    float const y = static_cast<float>(event.y()) * scale;

    if (event.buttonData() == MouseAction::DataDown) {
        if (action == MouseAction::ActionLeft) g_mouseEdges.push_back({true, x, y});
        event.cancel();
    } else if (action == MouseAction::ActionLeft) {
        // Releases pass through so the game clears any held-button state; the
        // matching press never reached it because it was cancelled above.
        g_mouseEdges.push_back({false, x, y});
    }
}

std::array<ll::event::ListenerPtr, 4> g_listeners;

} // namespace

void warmDocument() {
    seedDemoData();
    WidgetPtr root = Silica::CompileFile(kDocument, kEmbeddedSil);
    if (root) silica::imagepack::Warmup(root);
}

void openLog(std::filesystem::path const& path) {
    std::lock_guard lock(g_logMutex);
    g_log.open(path, std::ios::out | std::ios::trunc);
}

void logLine(char const* fmt, ...) {
    char    buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::lock_guard lock(g_logMutex);
    if (g_log) {
        g_log << buffer << '\n';
        g_log.flush();
    }
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

bool startOverlay() {
    loadPanel();

    SilicaHandlers::Register("cycle_theme", [] { cycleTheme(); });
    SilicaHandlers::Register("toggle_hint", [] { toggleHint(); });
    SilicaHandlers::Register("close_panel", [] { closePanel(true, "按钮"); });
    SilicaHandlers::Register("apply_settings", [] {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        setStatus("设置已应用");
    });
    static constexpr std::string_view kNavHandlers[] = {
        "nav_general",
        "nav_quality",
        "nav_performance",
        "nav_iris_settings",
        "nav_iris_shaders",
        "nav_vitrail_shaders",
    };
    for (std::string_view name : kNavHandlers) {
        SilicaHandlers::Register(std::string(name), [name] { selectNav(name); });
    }
    SilicaHandlers::Register("handle_opt", [](std::string_view value) {
        bool const on = value == "true";
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        logLine("[ui] checkbox -> %s", on ? "on" : "off");
        setStatus(on ? "复选框：开" : "复选框：关");
    });
    SilicaHandlers::Register("handle_vol", [](std::string_view value) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        std::string text(value);
        while (text.size() > 1 && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
        if (g_volumeValue) g_volumeValue->text = text;
        logLine("[ui] slider -> %.*s", static_cast<int>(value.size()), value.data());
        setStatus("界面缩放：" + text);
    });
    SilicaHandlers::Register("handle_text", [](std::string_view value) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        logLine("[ui] text -> %.*s", static_cast<int>(value.size()), value.data());
        setStatus("输入：" + std::string(value));
    });
    SilicaHandlers::Register("handle_submit", [](std::string_view value) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        logLine("[ui] submit -> %.*s", static_cast<int>(value.size()), value.data());
        setStatus("已提交：" + std::string(value));
    });
    SilicaHandlers::Register("handle_mode", [](std::string_view value) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        logLine("[ui] dropdown -> %.*s", static_cast<int>(value.size()), value.data());
        setStatus("模式索引：" + std::string(value));
    });

    try {
        auto& bus      = ll::event::EventBus::getInstance();
        g_listeners[0] = bus.emplaceListener<ll::event::BeforeUIRenderEvent>(
            [](ll::event::BeforeUIRenderEvent& event) { onRender(event); }
        );
        g_listeners[1] = bus.emplaceListener<ll::event::input::KeyInputEvent>([](ll::event::input::KeyInputEvent& event) {
            onKey(event);
        });
        g_listeners[2] = bus.emplaceListener<ll::event::input::MouseInputEvent>(
            [](ll::event::input::MouseInputEvent& event) { onMouse(event); }
        );
        g_listeners[3] = bus.emplaceListener<ll::event::ClientExitLevelEvent>([&](ll::event::ClientExitLevelEvent&) {
            g_gameplay.store(false, std::memory_order_relaxed);
            closePanel(false, "left the level");
        });
    } catch (...) {
        logLine("[ui] event subscription failed");
        stopOverlay();
        return false;
    }
    logLine("[ui] ready (press P)");
    return true;
}

void stopOverlay() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& listener : g_listeners) {
        if (listener) bus.removeListener(listener);
        listener.reset();
    }

    SilicaHandlers::Unregister("cycle_theme");
    SilicaHandlers::Unregister("toggle_hint");
    SilicaHandlers::Unregister("close_panel");
    SilicaHandlers::Unregister("apply_settings");
    for (std::string_view name : {std::string_view("nav_general"),
                                  std::string_view("nav_quality"),
                                  std::string_view("nav_performance"),
                                  std::string_view("nav_iris_settings"),
                                  std::string_view("nav_iris_shaders"),
                                  std::string_view("nav_vitrail_shaders")}) {
        SilicaHandlers::Unregister(std::string(name));
    }
    SilicaHandlers::Unregister("handle_opt");
    SilicaHandlers::Unregister("handle_vol");
    SilicaHandlers::Unregister("handle_text");
    SilicaHandlers::Unregister("handle_submit");
    SilicaHandlers::Unregister("handle_mode");

    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    std::shared_ptr<AbstractScene> scene = std::move(g_shell);
    g_shell.reset();
    resetPanelState();
    if (scene) {
        if (auto maybe = ll::service::getClientInstance()) {
            ISceneStack* stack = currentStack(*maybe);
            if (stack && stack->getTopScene() == scene.get()) stack->schedulePopScreen(1);
        }
    }
    g_root.reset();
    g_hint.reset();
    g_status.reset();
    g_volumeValue.reset();
}

} // namespace silica_test
