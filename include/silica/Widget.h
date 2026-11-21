#pragma once

#include "Animation.h"
#include "DataContext.h"
#include "DataStore.h"
#include "Expression.h"
#include "Style.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class MinecraftUIRenderContext;

namespace silica {

class Script;
struct ScriptEvent;
class SilicaWidget;
using WidgetPtr = std::shared_ptr<SilicaWidget>;

// Coordinates are absolute GUI units, not parent-relative: a container writes
// the final position into each child, which keeps the flat game renderer free
// of transform state.
//
// Every property the declarative layer can set goes through ApplyProperty, so a
// widget built from C++ and one built from .sil are configured identically.
//
// Widgets also implement DataObserver: a property bound to `@name` subscribes
// here and is re-applied when DataStore::Set changes one of its keys.
class SilicaWidget : public std::enable_shared_from_this<SilicaWidget>, public DataObserver {
public:
    virtual ~SilicaWidget() = default;

    float x      = 0.f;
    float y      = 0.f;
    float width  = 0.f;
    float height = 0.f;

    // Spacing. `margin` is the default gap on every side; each side may be
    // overridden individually, and a negative side falls back to `margin`.
    float margin       = 0.f;
    float marginTop    = -1.f;
    float marginRight  = -1.f;
    float marginBottom = -1.f;
    float marginLeft   = -1.f;

    float padding = 0.f; // inner inset a container keeps around its children

    // Stacking order among siblings. Larger draws later, so it also wins the
    // hit test; menus and tooltips use a high value to float above the rest.
    int zOrder = 0;

    std::string name; // optional identifier from .sil, for lookups

    UIStyle style;
    bool    visible = true;

    // Disabled widgets stay laid out and rendered but ignore pointer, wheel and
    // keyboard input; disabling a container disables its whole subtree. The
    // painted alpha is multiplied by `style.disabledOpacity`, and `@disabled`
    // is exposed to .sil so state rules, parts and expressions can restyle it.
    bool disabled = false;

    // Input/state flags maintained by PointerRouter and FocusManager; widgets
    // only paint them.
    bool hovered = false;
    bool pressed = false;
    bool focused = false;

    SilicaWidget* parent = nullptr;

    // A layout adds LeadTop/LeadLeft before a child when set, and GapBottom/
    // GapRight after it; `margin` is the default for the trailing side.
    float LeadTop() const { return marginTop >= 0.f ? marginTop : 0.f; }
    float LeadLeft() const { return marginLeft >= 0.f ? marginLeft : 0.f; }
    float GapBottom() const { return marginBottom >= 0.f ? marginBottom : margin; }
    float GapRight() const { return marginRight >= 0.f ? marginRight : margin; }

    // Virtual so a popup (Dropdown) can extend its hit area while expanded;
    // everything else keeps the plain rect test.
    [[nodiscard]] virtual bool Contains(float px, float py) const {
        return px >= x && py >= y && px < Right() && py < Bottom();
    }

    // Recomputes what depends on the placed geometry (auto sizes first, then
    // anything a subclass derives from x/y/width/height).
    virtual void Update() {}

    // Time-driven step: evaluates conditional style values, advances the
    // transition tracks and samples running keyframe animations. TickTree walks
    // the whole tree with it before Render.
    virtual void Tick(float deltaTime);

    // Render-only transform from keyframes; 1 when the widget is idle.
    [[nodiscard]] float AnimationScale() const { return m_keyframeScale; }

    virtual void Render(MinecraftUIRenderContext& context) = 0;

    // Mouse hooks. Down/up go to the deepest widget under the cursor and then
    // bubble to its ancestors until one returns true; the first handler wins
    // and receives every move until the button is released. Wheel bubbles the
    // same way, which lets a ScrollView claim it before the panel sees it.
    virtual bool OnMouseDown(float x, float y) {
        (void)x;
        (void)y;
        return false;
    }
    virtual bool OnMouseMove(float x, float y) {
        (void)x;
        (void)y;
        return false;
    }
    virtual bool OnMouseUp(float x, float y, bool inside) {
        (void)x;
        (void)y;
        (void)inside;
        return false;
    }
    virtual bool OnMouseWheel(float x, float y, float delta) {
        (void)x;
        (void)y;
        (void)delta;
        return false;
    }

    // Keyboard hook for the globally focused widget. Returns true when the key
    // was consumed; otherwise the host falls back to its own shortcuts.
    virtual bool OnKeyDown(int keyCode) {
        (void)keyCode;
        return false;
    }

    // Focus hooks. Focusable widgets join the keyboard ring and take the
    // global focus when clicked; OnFocusChanged is the only notification a
    // widget needs to implement.
    [[nodiscard]] virtual bool Focusable() const { return false; }
    virtual void              OnFocusChanged(bool isFocused) { (void)isFocused; }
    virtual bool              OnActivate() { return false; }

    // Applies one declarative key/value pair. Returns false when the key (or
    // its value) is not understood, which lets the compiler report typos.
    virtual bool ApplyProperty(std::string_view key, std::string_view value);

    // ---- reactive data ---------------------------------------------------

    // Attaches `expression` to `key` and applies its value immediately. The
    // binding is re-evaluated whenever DataStore::Set changes one of its keys;
    // expressions that also read widget state (`@checked`) are polled in Tick
    // because state does not go through the store. Returns false and fills
    // `error` when the expression does not parse.
    bool BindProperty(std::string_view key, std::string_view expression, std::string& error);

    // Attaches a literal-with-expressions value (`text: "{{@item.name}}"`).
    // Every `{{ ... }}` segment is an expression evaluated against the data
    // scope and store; the rest is copied verbatim. Segments are parsed once.
    bool BindTemplate(std::string_view key, std::string_view source, std::string& error);

    // Lexical scope injected by an enclosing ForEach (or a C++ caller). A
    // scope name shadows the store for the lifetime of the widget.
    void SetDataContext(std::shared_ptr<SilDataContext const> context);
    [[nodiscard]] std::shared_ptr<SilDataContext const> const& DataContext() const { return m_scope; }

    // Attaches a `{ ... }` script to any event property (`onClick`, `onChange`,
    // `onSubmit`, ...). The script assigns data keys through DataStore and may
    // read the payload of the event that fired it as `event.value` etc.
    bool SetScript(std::string_view key, std::string_view source, std::string& error);

    [[nodiscard]] bool HasScript(std::string_view event) const;

    // Runs the script attached to `event` with `payload` visible to the
    // expressions. Returns false when the widget has no such script, which lets
    // the control fall back to its C++ callback/handler name.
    bool FireScriptEvent(std::string_view event, ScriptEvent const& payload);

    // Runs the attached click script; false when there is none. Click handling
    // calls this before the C++ onClick hook.
    bool RunClickScript();

    // Two-way bindings: when the widget owns the edit (a real user toggle or
    // drag) and `property` is bound to a single data key (`checked: @opt`), the
    // new value is written back to the store. Returns false for computed
    // bindings, which stay read-only.
    bool WriteBackBinding(std::string_view property, Variant const& value);

    // DataObserver: delivered outside the store lock for each changed key.
    void OnDataChanged(std::string_view key) override;

    // Containers override this; leaves accept only SilicaPart decor layers, so
    // any widget can be re-skinned from .sil without a C++ subclass.
    virtual bool AddChild(std::shared_ptr<SilicaWidget> child);

    // Decor layers attached to this widget; see SilicaPart.
    [[nodiscard]] std::vector<WidgetPtr> const& Parts() const { return parts; }

    // Queried by part conditions ("when: checked"): the base knows the input
    // flags, widgets add their own states (checked, expanded, ...).
    [[nodiscard]] virtual bool HasState(std::string_view name) const;

    // True when this widget or any ancestor is disabled.
    [[nodiscard]] bool IsDisabled() const;

    // Alpha every paint call should use: `style.opacity` times the disabled
    // dim. Draw code calls this instead of reading style.opacity so disabled
    // styling needs no per-frame bookkeeping.
    [[nodiscard]] float PaintAlpha() const {
        return style.opacity * (IsDisabled() ? style.disabledOpacity : 1.f);
    }

    float Right() const { return x + width; }
    float Bottom() const { return y + height; }

protected:
    // Namespace a .sil expression sees: DataStore first, widget state second,
    // so a key always shadows a state of the same name.
    [[nodiscard]] Variant ResolveValue(std::string_view name) const;

    // Paints this widget's own background and border. Leaves do not use it
    // unless they look like a box; containers call it before their children.
    void PaintSelf(MinecraftUIRenderContext& context);

    // Subscribes this widget to the given store keys, skipping duplicates and
    // names already covered by the injected data scope.
    void WatchKeys(std::vector<std::string> const& keys);

    // Background this widget should paint right now: pressed > hovered >
    // normal, honoring the optional state colors from UIStyle.
    [[nodiscard]] Color StateFill() const;

    // Places and paints the decor layers whose `under` flag matches. Widgets
    // that draw their own background instead of PaintSelf call this directly;
    // PaintSelf does it for everyone else.
    void PaintParts(MinecraftUIRenderContext& context, bool under);

private:
    struct BoundProperty {
        std::string              property;
        Expression               expression;
        std::vector<std::string> keys;
        bool                     poll = false; // reads widget state, not just data
    };

    struct BoundTemplate {
        struct Segment {
            bool        expression = false;
            std::string literal;    // text copied verbatim
            Expression   value;     // evaluated when `expression` is set
        };

        std::string              property;
        std::vector<Segment>     segments;
        std::vector<std::string> keys;
        bool                     poll = false;
    };

    void EvaluateDynamic();
    void AdvanceTransitions(float deltaTime);
    void AdvanceKeyframes(float deltaTime);
    void AnimateColor(std::string const& key, Color const& target, Color& slot);
    void AnimateOpacity(float target);
    void RefreshOpacity();
    void SampleKeyframes(float progress);
    [[nodiscard]] bool EvalCondition(std::string_view condition) const;

    void ApplyBinding(BoundProperty const& binding, bool animate);
    void ApplyTemplate(BoundTemplate const& binding, bool animate);
    void EvaluateBindings(std::string_view onlyKey, bool animate);
    void EvaluateTemplates(std::string_view onlyKey, bool animate);
    void EvaluatePolledBindings();
    [[nodiscard]] bool ScopeHas(std::string_view name) const;

    std::vector<BoundProperty> m_bindings;
    std::vector<BoundTemplate> m_templates;
    std::set<std::string>      m_watchedKeys;
    std::map<std::string, std::shared_ptr<Script>> m_scripts; // event name -> script
    ScriptEvent const*         m_eventScope = nullptr;       // payload while a script runs
    std::shared_ptr<SilDataContext const> m_scope;
    bool                       m_polledBindings = false;

    std::vector<WidgetPtr> parts;

    // Conditional style values (`background: @checked ? a : b`) re-evaluated
    // every Tick; transition tracks interpolate when the target changes.
    std::vector<DynamicRule>              m_dynamic;
    std::map<std::string, TransitionSpec> m_transitions;
    std::map<std::string, ColorTrack>     m_colorTracks;
    FloatTrack                            m_opacityTrack;

    // Keyframes (`animation: pulse 1.5s infinite`).
    std::string m_animationName;
    float       m_animationDuration   = 1.f;
    int         m_animationIterations = 1; // -1 loops forever
    float       m_animationTime       = 0.f;
    bool        m_animationDone       = false;
    KeyframeSet m_keyframes;
    float       m_keyframeOpacity     = 1.f;
    float       m_keyframeScale       = 1.f;

    float m_baseOpacity    = 1.f;
    bool  m_opacityManaged = false;
    bool  m_inDynamicEval  = false;
};

// Names used by the file-loading API; the framework grew up around WidgetPtr.
using UIWidget    = SilicaWidget;
using UIWidgetPtr = WidgetPtr;

} // namespace silica
