#pragma once

#include <source2mod/ISource2ModTypes.h>

// Each C++ plugin has its own instance of the interfaces that care who calls;
// the instance knows its plugin, so no call in the API names one. Generated
// from the SDK by tools/pluginifaces/pluginifaces.py.
namespace pluginifaces
{
    /// A new instance for this plugin; null for an interface that does not care.
    void* Create(const char* iface, PluginId owner);

    /// This plugin's instance, or @p shared when the interface has only that
    /// one. Instances stay until the core goes: a plugin's globals point at
    /// them while its library closes.
    void* ForPlugin(const char* iface, PluginId owner, void* shared);
}
