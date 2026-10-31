#pragma once

// ISource2ModAPI as plugin API 8 had it, slot for slot, and the object a plugin
// built against 8 is handed: every call goes on to the current interface.
#include <source2mod/ISource2ModPlugin.h>
#include <source2mod/ISource2ModApi.h>

#include <cstdarg>
#include <cstdio>

namespace compat
{
class ISource2ModAPI008
{
public:
    virtual ~ISource2ModAPI008() = default;
    virtual void Log(const char* msg, ...) = 0;
    virtual void ConPrint(const char* msg) = 0;
    virtual void ConPrintf(const char* fmt, ...) = 0;
    virtual void AddListener(ISource2ModListener* pListener) = 0;
    virtual void* QueryInterface(CreateInterfaceFn fn, const char* iface, int min = -1) = 0;
    virtual void* InterfaceSearch(CreateInterfaceFn fn, const char* iface, int max, int* ret) = 0;
    virtual int FormatIface(char iface[], size_t maxlength) = 0;
    virtual CreateInterfaceFn GetEngineFactory(bool syn = true) = 0;
    virtual CreateInterfaceFn GetPhysicsFactory(bool syn = true) = 0;
    virtual CreateInterfaceFn GetFileSystemFactory(bool syn = true) = 0;
    virtual CreateInterfaceFn GetServerFactory(bool syn = true) = 0;
    virtual void* Source2ModFactory(const char* iface, int* ret, PluginId* id) = 0;
    virtual void* MetaFactory(const char* iface, int* ret, PluginId* id) = 0;
    virtual ISource2ModAddresses* Addresses() = 0;
    virtual ISource2ModCommands* Commands() = 0;
    virtual ISource2ModConVars* ConVars() = 0;
    virtual ISource2ModCustomHud* CustomHud() = 0;
    virtual ISource2ModEntities* Entities() = 0;
    virtual ISource2ModEvents* Events() = 0;
    virtual ISource2ModGameConfig* GameConfig() = 0;
    virtual ISource2ModGameSystems* GameSystems() = 0;
    virtual ISource2ModHTTP* HTTP() = 0;
    virtual ISource2ModMenus* Menus() = 0;
    virtual ISource2ModSQL* SQL() = 0;
    virtual ISource2ModNetworkMessages* NetworkMessages() = 0;
    virtual ISource2ModPaths* Paths() = 0;
    virtual ISource2ModScheduler* Scheduler() = 0;
    virtual ISource2ModSounds* Sounds() = 0;
    virtual ISource2ModTrace* Trace() = 0;
    virtual ISource2ModTransmit* Transmit() = 0;
    virtual ISource2Server* GetSource2Server() = 0;
    virtual ISource2ServerConfig* GetSource2ServerConfig() = 0;
    virtual ISource2GameClients* GetSource2GameClients() = 0;
    virtual ISource2GameEntities* GetSource2GameEntities() = 0;
    virtual IVEngineServer2* GetEngineServer() = 0;
    virtual ICvar* GetCVar() = 0;
    virtual CSchemaSystem* GetSchemaSystem() = 0;
    virtual IGameResourceService* GetGameResourceService() = 0;
    virtual INetworkServerService* GetNetworkServerService() = 0;
    virtual INetworkSystem* GetNetworkSystem() = 0;
    virtual INetworkMessages* GetNetworkMessages() = 0;
    virtual INetworkStringTableContainer* GetNetworkStringTableServer() = 0;
    virtual IEngineServiceMgr* GetEngineServiceMgr() = 0;
    virtual IHostStateMgr* GetHostStateMgr() = 0;
    virtual ILocalize* GetLocalize() = 0;
    virtual IFileSystem* GetFileSystem() = 0;
    virtual IGameEventSystem* GetGameEventSystem() = 0;
    virtual IGameEventManager2* GetGameEventManager() = 0;
    virtual CGameEntitySystem* GetEntitySystem() = 0;
    virtual CGlobalVars* GetGlobalVars() = 0;
    virtual const char* GetBaseDir() = 0;
    virtual size_t Format(char* buffer, size_t maxlength, const char* format, ...) = 0;
    virtual size_t FormatArgs(char* buffer, size_t maxlength, const char* format, va_list ap) = 0;
    virtual ISource2ModHooks* Hooks() = 0;
    virtual bool FollowsServerGuidelines() = 0;
};

class PluginApi008 final : public ISource2ModAPI008
{
public:
    explicit PluginApi008(ISource2ModAPI* api) : m_api(api) {}

    void Log(const char* msg, ...) override
    {
        char buffer[2048];
        va_list ap;
        va_start(ap, msg);
        vsnprintf(buffer, sizeof(buffer), msg, ap);
        va_end(ap);
        m_api->Log("%s", buffer);
    }

    void ConPrintf(const char* fmt, ...) override
    {
        char buffer[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, ap);
        va_end(ap);
        m_api->ConPrint(buffer);
    }

    size_t Format(char* buffer, size_t maxlength, const char* format, ...) override
    {
        va_list ap;
        va_start(ap, format);
        const size_t written = m_api->FormatArgs(buffer, maxlength, format, ap);
        va_end(ap);
        return written;
    }

    void ConPrint(const char* msg) override { m_api->ConPrint(msg); }
    void AddListener(ISource2ModListener* pListener) override { m_api->AddListener(pListener); }
    void* QueryInterface(CreateInterfaceFn fn, const char* iface, int min) override { return m_api->QueryInterface(fn, iface, min); }
    void* InterfaceSearch(CreateInterfaceFn fn, const char* iface, int max, int* ret) override { return m_api->InterfaceSearch(fn, iface, max, ret); }
    int FormatIface(char iface[], size_t maxlength) override { return m_api->FormatIface(iface, maxlength); }
    CreateInterfaceFn GetEngineFactory(bool syn) override { return m_api->GetEngineFactory(syn); }
    CreateInterfaceFn GetPhysicsFactory(bool syn) override { return m_api->GetPhysicsFactory(syn); }
    CreateInterfaceFn GetFileSystemFactory(bool syn) override { return m_api->GetFileSystemFactory(syn); }
    CreateInterfaceFn GetServerFactory(bool syn) override { return m_api->GetServerFactory(syn); }
    void* Source2ModFactory(const char* iface, int* ret, PluginId* id) override { return m_api->Source2ModFactory(iface, ret, id); }
    void* MetaFactory(const char* iface, int* ret, PluginId* id) override { return m_api->MetaFactory(iface, ret, id); }
    ISource2ModAddresses* Addresses() override { return m_api->Addresses(); }
    ISource2ModCommands* Commands() override { return m_api->Commands(); }
    ISource2ModConVars* ConVars() override { return m_api->ConVars(); }
    ISource2ModCustomHud* CustomHud() override { return m_api->CustomHud(); }
    ISource2ModEntities* Entities() override { return m_api->Entities(); }
    ISource2ModEvents* Events() override { return m_api->Events(); }
    ISource2ModGameConfig* GameConfig() override { return m_api->GameConfig(); }
    ISource2ModGameSystems* GameSystems() override { return m_api->GameSystems(); }
    ISource2ModHTTP* HTTP() override { return m_api->HTTP(); }
    ISource2ModMenus* Menus() override { return m_api->Menus(); }
    ISource2ModSQL* SQL() override { return m_api->SQL(); }
    ISource2ModNetworkMessages* NetworkMessages() override { return m_api->NetworkMessages(); }
    ISource2ModPaths* Paths() override { return m_api->Paths(); }
    ISource2ModScheduler* Scheduler() override { return m_api->Scheduler(); }
    ISource2ModSounds* Sounds() override { return m_api->Sounds(); }
    ISource2ModTrace* Trace() override { return m_api->Trace(); }
    ISource2ModTransmit* Transmit() override { return m_api->Transmit(); }
    ISource2Server* GetSource2Server() override { return m_api->GetSource2Server(); }
    ISource2ServerConfig* GetSource2ServerConfig() override { return m_api->GetSource2ServerConfig(); }
    ISource2GameClients* GetSource2GameClients() override { return m_api->GetSource2GameClients(); }
    ISource2GameEntities* GetSource2GameEntities() override { return m_api->GetSource2GameEntities(); }
    IVEngineServer2* GetEngineServer() override { return m_api->GetEngineServer(); }
    ICvar* GetCVar() override { return m_api->GetCVar(); }
    CSchemaSystem* GetSchemaSystem() override { return m_api->GetSchemaSystem(); }
    IGameResourceService* GetGameResourceService() override { return m_api->GetGameResourceService(); }
    INetworkServerService* GetNetworkServerService() override { return m_api->GetNetworkServerService(); }
    INetworkSystem* GetNetworkSystem() override { return m_api->GetNetworkSystem(); }
    INetworkMessages* GetNetworkMessages() override { return m_api->GetNetworkMessages(); }
    INetworkStringTableContainer* GetNetworkStringTableServer() override { return m_api->GetNetworkStringTableServer(); }
    IEngineServiceMgr* GetEngineServiceMgr() override { return m_api->GetEngineServiceMgr(); }
    IHostStateMgr* GetHostStateMgr() override { return m_api->GetHostStateMgr(); }
    ILocalize* GetLocalize() override { return m_api->GetLocalize(); }
    IFileSystem* GetFileSystem() override { return m_api->GetFileSystem(); }
    IGameEventSystem* GetGameEventSystem() override { return m_api->GetGameEventSystem(); }
    IGameEventManager2* GetGameEventManager() override { return m_api->GetGameEventManager(); }
    CGameEntitySystem* GetEntitySystem() override { return m_api->GetEntitySystem(); }
    CGlobalVars* GetGlobalVars() override { return m_api->GetGlobalVars(); }
    const char* GetBaseDir() override { return m_api->GetBaseDir(); }
    size_t FormatArgs(char* buffer, size_t maxlength, const char* format, va_list ap) override { return m_api->FormatArgs(buffer, maxlength, format, ap); }
    ISource2ModHooks* Hooks() override { return m_api->Hooks(); }
    bool FollowsServerGuidelines() override { return m_api->FollowsServerGuidelines(); }

private:
    ISource2ModAPI* m_api;
};
}
