// Runtime check of the .sil front end: no game headers, no window, so the test
// binary runs as part of the framework build. Exits non-zero on any failure.

#include "silica/Property.h"
#include "silica/SilParser.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace silica;

namespace {

int failures = 0;

void check(bool condition, char const* what) {
    if (condition) return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void testSample() {
    constexpr char kSample[] =
        "// Silica demo\n"
        "Container {\n"
        "    layout: vertical;\n"
        "    background: #000000AA;\n"
        "    padding: 10;\n"
        "    Label {\n"
        "        text: \"Silica 声明式 UI\";\n"
        "        color: #FFFFFF;\n"
        "        font-size: 1.5;\n"
        "        margin-bottom: 10;\n"
        "    }\n"
        "    Button {\n"
        "        text: \"点击我\";\n"
        "        background: #4CAF50;\n"
        "        color: #FFF;\n"
        "        width: 100;\n"
        "        height: 24;\n"
        "        onClick: \"handle_button_click\";\n"
        "    }\n"
        "}\n";

    SilParser parser(kSample);
    SilDocument document = parser.Parse();

    check(document.diagnostics.empty(), "sample has no diagnostics");
    check(document.roots.size() == 1, "sample has one root");
    if (document.roots.empty()) return;

    SilNode const& root = document.roots[0];
    check(root.type == "Container", "root is a Container");
    check(root.line == 2, "root line is 2");
    check(root.props.size() == 3, "container has three properties");
    check(root.props.at("layout") == "vertical", "layout value");
    check(root.props.at("background") == "#000000AA", "background value");
    check(root.props.at("padding") == "10", "padding value");
    check(root.children.size() == 2, "container has two children");
    if (root.children.size() != 2) return;

    SilNode const& label = root.children[0];
    check(label.type == "Label", "first child is a Label");
    check(label.props.at("text") == "Silica 声明式 UI", "label text keeps UTF-8");
    check(label.props.at("font-size") == "1.5", "label font-size");
    check(label.props.at("margin-bottom") == "10", "label margin-bottom");

    SilNode const& button = root.children[1];
    check(button.type == "Button", "second child is a Button");
    check(button.props.at("onClick") == "handle_button_click", "handler name is unquoted");
    check(button.props.at("width") == "100", "button width");
}

void testReactive() {
    constexpr char kReactive[] =
        "Container {\n"
        "    Label {\n"
        "        text: @user_name;\n"
        "        color: @theme_color;\n"
        "        visible: @health > 0;\n"
        "    }\n"
        "    Label {\n"
        "        text: @health > 0 ? \"存活\" : \"死亡\";\n"
        "    }\n"
        "    Button {\n"
        "        text: \"加血\";\n"
        "        onClick: { @health = @health + 10; }\n"
        "    }\n"
        "}\n";

    SilParser   parser(kReactive);
    SilDocument document = parser.Parse();

    check(document.diagnostics.empty(), "reactive sample has no diagnostics");
    check(document.roots.size() == 1, "reactive sample has one root");
    if (document.roots.empty()) return;

    SilNode const& root = document.roots[0];
    check(root.children.size() == 3, "reactive root has three children");
    if (root.children.size() != 3) return;

    SilNode const& label = root.children[0];
    check(label.bindings.size() == 3, "the label carries three bindings");
    check(!label.props.contains("text"), "a bound value is not stored as a literal property");
    check(label.bindings.at("text") == "@user_name", "bare data binding");
    check(label.bindings.at("color") == "@theme_color", "color binding");
    check(label.bindings.at("visible") == "@health > 0", "relational binding keeps its operator");

    SilNode const& conditional = root.children[1];
    check(
        conditional.bindings.at("text") == "@health > 0 ? \"存活\" : \"死亡\"",
        "the binding keeps quotes and the ternary exactly as written"
    );

    SilNode const& button = root.children[2];
    check(button.props.at("text") == "加血", "literal text still parses");
    check(!button.props.contains("onClick"), "the script is not stored as a property");
    check(button.scripts.size() == 1, "the button carries one script");
    check(
        button.scripts.at("onClick") == "@health = @health + 10;",
        "the script body keeps its statement"
    );

    constexpr char kEvents[] =
        "Checkbox {\n"
        "    checked: @option_a;\n"
        "    onChange: { @count += 1; if (event.checked) { @theme = \"on\"; } else { @theme = \"off\"; } }\n"
        "}\n"
        "Button {\n"
        "    onClick: { $next = @health + 1;\n"
        "               if ($next > 100) { @health = 100; } else { @health = $next; } }\n"
        "}\n";

    SilParser   eventParser(kEvents);
    SilDocument events = eventParser.Parse();
    check(events.diagnostics.empty(), "event script sample has no diagnostics");
    check(events.roots.size() == 2, "event script sample has two roots");
    if (events.roots.size() == 2) {
        SilNode const& box = events.roots[0];
        check(box.bindings.at("checked") == "@option_a", "two-way binding keeps its data key");
        check(box.scripts.size() == 1, "onChange is stored as a script");
        check(
            box.scripts.at("onChange")
                == "@count += 1; if (event.checked) { @theme = \"on\"; } else { @theme = \"off\"; }",
            "the change script keeps its body verbatim"
        );

        SilNode const& localButton = events.roots[1];
        check(
            localButton.scripts.at("onClick")
                == "$next = @health + 1;\n               if ($next > 100) { @health = 100; } else { @health = $next; }",
            "multi-line scripts with locals keep their body verbatim"
        );
    }
}

void testForEach() {
    constexpr char kList[] =
        "Container {\n"
        "    layout: vertical;\n"
        "    ForEach (data: @search_results, item: @item, index: @idx) {\n"
        "        Button {\n"
        "            text: \"{{@item.name}} - {{@item.count}}\";\n"
        "            background: @idx % 2 == 0 ? \"#222222\" : \"#333333\";\n"
        "            onClick: { @selected_item = @item.id; }\n"
        "        }\n"
        "    }\n"
        "}\n";

    SilParser   parser(kList);
    SilDocument document = parser.Parse();

    check(document.diagnostics.empty(), "ForEach sample has no diagnostics");
    check(document.roots.size() == 1, "ForEach sample has one root");
    if (document.roots.empty()) return;

    SilNode const& root = document.roots[0];
    check(root.children.size() == 1, "the list container holds the repeater");
    if (root.children.size() != 1) return;

    SilNode const& row = root.children[0];
    check(row.type == "ForEach", "ForEach keeps its component name");
    check(row.params.size() == 3, "ForEach carries three parameters");
    check(!row.props.contains("data"), "parameters are not literal properties");
    check(row.params.at("data") == "@search_results", "data parameter keeps its source");
    check(row.params.at("item") == "@item", "item parameter is recorded");
    check(row.params.at("index") == "@idx", "index parameter is recorded");
    check(row.children.size() == 1, "the template body is a child node");

    if (row.children.size() != 1) return;
    SilNode const& button = row.children[0];
    check(
        button.templates.at("text") == "{{@item.name}} - {{@item.count}}",
        "interpolated text is stored as a template, not a property"
    );
    check(!button.props.contains("text"), "a template is not also a literal");
    check(
        button.bindings.at("background") == "@idx % 2 == 0 ? \"#222222\" : \"#333333\"",
        "the modulo operator survives in a binding"
    );
    check(button.scripts.count("onClick") == 1, "the row click stays a script");
}

void testDefinitions() {
    constexpr char kDefinitions[] =
        "Var gutter: 6;\n"
        "Var shade: #222222;\n"
        "Style Base {\n"
        "    background: $shade;\n"
        "    padding: 4;\n"
        "}\n"
        "Style Card extends Base {\n"
        "    padding: 8;\n"
        "    border: 1 solid #303030;\n"
        "}\n"
        "Component Stat(label, value: 0) {\n"
        "    Label { text: \"@label\"; }\n"
        "    Slot { }\n"
        "}\n"
        "Container {\n"
        "    class: \"card\";\n"
        "    Stat(label: \"生命\", value: @health) {\n"
        "        Button { text: \"+1\"; }\n"
        "    }\n"
        "}\n";

    SilParser   parser(kDefinitions);
    SilDocument document = parser.Parse();

    check(document.diagnostics.empty(), "definitions sample has no diagnostics");
    check(document.vars.size() == 2, "two variables are recorded");
    check(document.vars.at("gutter") == "6", "variable keeps its value");
    check(document.vars.at("shade") == "#222222", "variable keeps a color literal");

    check(document.styles.size() == 2, "two styles are recorded");
    check(document.styles.at("Card").base == "Base", "Style records its extends base");
    check(document.styles.at("Card").values.props.at("padding") == "8", "derived style value");
    check(
        document.styles.at("Base").values.props.at("background") == "$shade",
        "a style keeps the variable reference for compile time"
    );

    check(document.components.size() == 1, "one component is recorded");
    SilComponentDef const& stat = document.components.at("Stat");
    check(stat.params.size() == 2, "component parameters are recorded");
    check(stat.params[0].name == "label" && stat.params[0].defaultValue.empty(), "required parameter");
    check(stat.params[1].name == "value" && stat.params[1].defaultValue == "0", "default parameter value");
    check(stat.body.size() == 2, "component body holds its nodes");
    if (stat.body.size() == 2) {
        check(stat.body[0].type == "Label", "component body keeps the first node");
        check(stat.body[0].props.at("text") == "@label", "parameter reference survives parsing");
        check(property::ToLower(stat.body[1].type) == "slot", "slot is kept in the body");
    }

    check(document.roots.size() == 1, "definitions do not become roots");
    if (document.roots.empty()) return;

    SilNode const& root = document.roots[0];
    check(root.type == "Container", "the widget root follows the definitions");
    check(root.props.at("class") == "card", "class directive is parsed as a property first");
    check(root.children.size() == 1, "the container holds the instantiation");
    if (root.children.size() != 1) return;

    SilNode const& instance = root.children[0];
    check(instance.type == "Stat", "the instantiation keeps the component name");
    check(instance.params.at("label") == "\"生命\"", "a quoted argument keeps its quotes");
    check(instance.params.at("value") == "@health", "a reactive argument keeps its expression");
    check(instance.children.size() == 1, "slot content is stored as children");
}

void testRecovery() {
    constexpr char kBroken[] =
        "Container {\n"
        "    layout vertical;\n"
        "    Label {\n"
        "        text: \"x\";\n"
        "}\n";

    SilParser   parser(kBroken);
    SilDocument document = parser.Parse();

    check(!document.diagnostics.empty(), "broken source reports diagnostics");
    bool missingColon = false;
    bool missingBrace = false;
    for (auto const& diagnostic : document.diagnostics) {
        check(diagnostic.line >= 1 && !diagnostic.message.empty(), "diagnostic has a position and message");
        if (diagnostic.line == 2 && diagnostic.message.find("':'") != std::string::npos) missingColon = true;
        if (diagnostic.message.find("missing '}'") != std::string::npos) missingBrace = true;
    }
    check(missingColon, "missing ':' reported on line 2");
    check(missingBrace, "missing '}' reported");
    check(document.roots.size() == 1, "recovery still yields the root");

    SilParser   unterminated("Label { text: \"oops; \n}\n");
    SilDocument u = unterminated.Parse();
    bool        reported = false;
    for (auto const& diagnostic : u.diagnostics) {
        if (diagnostic.message.find("unterminated string") != std::string::npos) reported = true;
    }
    check(reported, "unterminated string reported");

    // A value directly before '}' is accepted without a trailing ';'.
    SilParser   relaxed("Panel { width: 10 }");
    SilDocument r = relaxed.Parse();
    check(r.diagnostics.empty(), "omitted final semicolon is accepted");
    check(r.roots.size() == 1 && r.roots[0].props.at("width") == "10", "relaxed value parsed");
}

void testRobustness() {
    std::string garbage(4096, '#');
    SilParser   parser(garbage);
    SilDocument document = parser.Parse();
    check(document.roots.empty(), "garbage yields no roots");
    check(!document.diagnostics.empty(), "garbage yields diagnostics");

    std::string deep;
    for (int i = 0; i < 500; ++i) deep += "A { ";
    for (int i = 0; i < 500; ++i) deep += "} ";

    SilParser   nested(deep);
    SilDocument result = nested.Parse();
    bool        capped = false;
    for (auto const& diagnostic : result.diagnostics) {
        if (diagnostic.message.find("too deeply") != std::string::npos) capped = true;
    }
    check(capped, "deep nesting is capped instead of overflowing the stack");
}

void testValues() {
    auto color = property::ParseColor("#4CAF50");
    check(color.has_value(), "#4CAF50 parses");
    if (color) {
        check(near(color->r, 0x4C / 255.f), "red channel");
        check(near(color->g, 0xAF / 255.f), "green channel");
        check(near(color->b, 0x50 / 255.f), "blue channel");
        check(near(color->a, 1.f), "opaque alpha");
    }

    auto faded = property::ParseColor("#000000AA");
    check(faded.has_value(), "#000000AA parses");
    if (faded) check(near(faded->a, 0xAA / 255.f), "alpha channel is last");

    auto shorthand = property::ParseColor("#FFF");
    check(shorthand.has_value(), "#FFF parses");
    if (shorthand) check(near(shorthand->r, 1.f) && near(shorthand->a, 1.f), "#FFF expands");

    check(!property::ParseColor("#GGG").has_value(), "invalid hex is rejected");
    check(!property::ParseColor("").has_value(), "empty color is rejected");

    auto length = property::ParseNumber("10px");
    check(length.has_value() && near(*length, 10.f), "px suffix is stripped");
    auto negative = property::ParseNumber("-1.5");
    check(negative.has_value() && near(*negative, -1.5f), "negative numbers parse");
    check(!property::ParseNumber("10 20").has_value(), "trailing tokens are rejected");

    auto border = property::ParseBorder("1 solid #FFFFFF");
    check(border.has_value() && border->width && near(*border->width, 1.f), "border width");
    check(border && border->color && near(border->color->r, 1.f), "border color");
}

} // namespace

int main() {
    testSample();
    testReactive();
    testForEach();
    testDefinitions();
    testRecovery();
    testRobustness();
    testValues();

    if (failures == 0) {
        std::printf("PARSER TEST PASS\n");
        return 0;
    }
    std::printf("PARSER TEST FAIL (%d)\n", failures);
    return 1;
}
