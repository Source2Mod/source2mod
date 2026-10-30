#include "core/plugins/pluginifaces.h"

#include <map>
#include <string>
#include <utility>

namespace pluginifaces
{
    void* ForPlugin(const char* iface, PluginId owner, void* shared)
    {
        static std::map<std::pair<PluginId, std::string>, void*> s_instances;

        const auto key = std::make_pair(owner, std::string(iface));
        const auto found = s_instances.find(key);
        if (found != s_instances.end())
            return found->second;

        void* instance = Create(iface, owner);
        if (!instance)
            instance = shared;
        s_instances.emplace(key, instance);
        return instance;
    }
}
