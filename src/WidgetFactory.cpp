#include "silica/WidgetFactory.h"

#include "silica/Button.h"
#include "silica/Checkbox.h"
#include "silica/Container.h"
#include "silica/Dropdown.h"
#include "silica/ForEach.h"
#include "silica/Image.h"
#include "silica/Label.h"
#include "silica/Panel.h"
#include "silica/Part.h"
#include "silica/Property.h"
#include "silica/ScrollView.h"
#include "silica/Slider.h"
#include "silica/TextBox.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace silica {
namespace {

struct FactoryRegistry {
    std::mutex                                                      mutex;
    std::unordered_map<std::string, WidgetFactory::Creator>         creators;
};

FactoryRegistry& registry() {
    static FactoryRegistry instance;
    return instance;
}

std::string keyOf(std::string_view type) {
    return property::ToLower(property::Trim(type));
}

} // namespace

void WidgetFactory::Register(std::string type, Creator creator) {
    std::string key = keyOf(type);
    if (key.empty() || !creator) return;
    std::lock_guard lock(registry().mutex);
    registry().creators[std::move(key)] = std::move(creator);
}

bool WidgetFactory::Unregister(std::string_view type) {
    std::string const key = keyOf(type);
    if (key.empty()) return false;
    std::lock_guard lock(registry().mutex);
    return registry().creators.erase(key) > 0;
}

WidgetPtr WidgetFactory::Create(std::string_view type) {
    RegisterBuiltins();

    Creator creator;
    {
        std::string const key = keyOf(type);
        std::lock_guard   lock(registry().mutex);
        auto const        found = registry().creators.find(key);
        if (found == registry().creators.end()) return nullptr;
        creator = found->second;
    }
    try {
        return creator ? creator() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

bool WidgetFactory::Has(std::string_view type) {
    std::string const key = keyOf(type);
    std::lock_guard   lock(registry().mutex);
    return registry().creators.find(key) != registry().creators.end();
}

std::vector<std::string> WidgetFactory::Types() {
    std::lock_guard           lock(registry().mutex);
    std::vector<std::string> types;
    types.reserve(registry().creators.size());
    for (auto const& entry : registry().creators) types.push_back(entry.first);
    return types;
}

void WidgetFactory::RegisterBuiltins() {
    static std::once_flag once;
    std::call_once(once, [] {
        // A type registered before the first compile wins, so a project can
        // replace a builtin without editing the framework.
        auto const add = [](char const* type, Creator creator) {
            if (!Has(type)) Register(type, std::move(creator));
        };
        add("Container", [] { return std::make_shared<SilicaContainer>(); });
        add("Panel", [] { return std::make_shared<SilicaPanel>(); });
        add("Label", [] { return std::make_shared<SilicaLabel>(); });
        add("Button", [] { return std::make_shared<SilicaButton>(); });
        add("Image", [] { return std::make_shared<SilicaImage>(); });
        add("Img", [] { return std::make_shared<SilicaImage>(); });
        add("ScrollView", [] { return std::make_shared<SilicaScrollView>(); });
        add("Scroll", [] { return std::make_shared<SilicaScrollView>(); });
        add("Checkbox", [] { return std::make_shared<SilicaCheckbox>(); });
        add("Slider", [] { return std::make_shared<SilicaSlider>(); });
        add("TextBox", [] { return std::make_shared<SilicaTextBox>(); });
        add("TextInput", [] { return std::make_shared<SilicaTextBox>(); });
        add("Input", [] { return std::make_shared<SilicaTextBox>(); });
        add("Dropdown", [] { return std::make_shared<SilicaDropdown>(); });
        add("Select", [] { return std::make_shared<SilicaDropdown>(); });
        add("ForEach", [] { return std::make_shared<SilicaForEach>(); });
        add("Part", [] { return std::make_shared<SilicaPart>(); });
        add("Layer", [] { return std::make_shared<SilicaPart>(); });
    });
}

} // namespace silica
