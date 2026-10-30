#!/usr/bin/env python3
"""Generates src/core/plugins/gen/pluginifaces.gen.cpp and
src/sourcepawn/gen/nativesforplugin.gen.inc.

Each C++ plugin gets its own instance of the interfaces below: the object
remembers the plugin and hands it to the manager's method that takes an owner,
so no call in the API names a plugin. Every other method only forwards.

Run it after changing a listed interface:
    python3 tools/pluginifaces/pluginifaces.py <sdk>/public/source2mod
A method with a same-named manager method taking `PluginId owner` first (or
<Name>For) gets the owner; see OWNER_RE.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))

# Interface, version macro, manager expression, manager type, manager sources.
IFACES = [
    ("Clients", "CLIENTS", "clients::clientsManager", "core/game/clients"),
    ("Commands", "COMMANDS", "commands::commandsManager", "core/game/commands"),
    ("Configs", "CONFIGS", "configs::configsManager", "core/data/configs"),
    ("ConVars", "CONVARS", "convars::convarsManager", "core/game/convars"),
    ("Cookies", "COOKIES", "cookies::cookiesManager", "core/data/cookies"),
    ("Crash", "CRASH", "crashhandler::crashListeners", "core/runtime/crashhandler"),
    ("CustomHud", "CUSTOMHUD", "customhud::customHudManager", "core/ui/customhud"),
    ("Entities", "ENTITIES", "entities::entitiesManager", "core/game/entities"),
    ("Events", "EVENTS", "events::eventManager", "core/game/events"),
    ("Functions", "FUNCTIONS", "functions::functionsManager", "core/game/functions"),
    ("GameSystems", "GAMESYSTEMS", "gamesystems::gameSystemsManager", "core/game/gamesystems"),
    ("Hooks", "HOOKS", "hooks::hooksManager", "core/hooks/hooks"),
    ("HTTP", "HTTP", "http::httpManager", "core/net/http"),
    ("Menus", "MENUS", "menus::menuManager", "core/ui/menus"),
    ("NetworkMessages", "NETWORKMESSAGES", "networkmessages::networkMessagesManager", "core/game/networkmessages"),
    ("Paths", "PATHS", "paths::pathsManager", "core/data/paths"),
    ("Permissions", "PERMISSIONS", "permissions::permissionsManager", "core/game/permissions"),
    ("Scheduler", "SCHEDULER", "scheduler::schedulerManager", "core/runtime/scheduler"),
    ("Scripts", "SCRIPTS", "scripts::scriptsManager", "core/game/scripts"),
    ("Sounds", "SOUNDS", "sounds::soundsManager", "core/game/sounds"),
    ("SQL", "SQL", "mysql::mysqlManager", "core/data/mysql"),
    ("Targeting", "TARGETING", "targeting::targetingManager", "core/game/targeting"),
    ("Translations", "TRANSLATIONS", "translations::translationsManager", "core/game/translations"),
    ("Transmit", "TRANSMIT", "transmit::transmitManager", "core/game/transmit"),
    ("Utils", "UTILS", "utils::utilsManager", "core/runtime/utils"),
    ("WebSockets", "WEBSOCKETS", "websockets::webSocketsManager", "core/net/websockets"),
]
# Lives in a .cpp: its instance is generated into an include of that file.
NATIVES = ("Natives", "NATIVES", "g_nativesApi", "sourcepawn/pluginsys")

DECL = re.compile(r"virtual\s+([^;(){}]*?)\b(\w+)\(([^;{}]*?)\)(\s*const)?\s*=\s*0\s*;", re.S)
OWNER_RE = r"\b%s(?:For)?\(\s*PluginId \w+(?:,\s*([^;{}]*?))?\)"


def split(params):
    out, depth, cur = [], 0, ""
    for c in params:
        depth += c in "<(["
        depth -= c in ">)]"
        if c == "," and not depth:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def body_of(text, name):
    m = re.search(r"class\s+(?:\w+\s+)?%s\b[^;{]*\{" % name, text)
    if not m:
        sys.exit("no class " + name)
    i, depth = m.end(), 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text[m.end():i], flags=re.S)


def top_level(body):
    """The class body without nested braces (inline methods, nested types)."""
    out, depth = "", 0
    for c in body:
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            out += ";" if depth == 0 else ""
        elif depth == 0:
            out += c
    return out


def manager_text(src):
    text = ""
    for ext in (".h", ".cpp"):
        path = os.path.join(ROOT, "src", src + ext)
        if os.path.exists(path):
            text += open(path, encoding="utf-8").read()
    return text


def param_type(p):
    """A parameter's type: no name, no default, spacing normalized."""
    p = p.split("=")[0].strip()
    return re.sub(r"\bconst\b|&|\s+", "", re.sub(r"\b\w+$", "", p))


def owner_call(mgr_text, name, types):
    for m in re.finditer(OWNER_RE % name, mgr_text, re.S):
        if [param_type(p) for p in split(m.group(1) or "")] == types:
            return re.search(r"\b(%s(?:For)?)\(" % name, m.group(0)).group(1)
    return None


def methods(sdk, iface, mgr_expr, src):
    text = open(os.path.join(sdk, "ISource2Mod%s.h" % iface), encoding="utf-8").read()
    mgr = manager_text(src)
    lines = []
    owned = 0
    for ret, name, params, const in DECL.findall(top_level(body_of(text, "ISource2Mod" + iface))):
        decls = []
        args = []
        for i, p in enumerate(split(re.sub(r"\s+", " ", params))):
            p = p.split("=")[0].strip()
            if p == "...":
                sys.exit("variadic %s::%s: write it by hand" % (iface, name))
            arr = p.endswith("[]")
            m = re.match(r"(.*[\s*&])(\w+)$", p[:-2] if arr else p)
            if not m or m.group(1).strip() in ("", "const", "unsigned"):
                sys.exit("unnamed parameter in %s::%s: %s" % (iface, name, p))
            decls.append(p)
            args.append("static_cast<decltype(%s)&&>(%s)" % (m.group(2), m.group(2)))
        target = owner_call(mgr, name, [param_type(p) for p in decls])
        if target:
            owned += 1
            args.insert(0, "m_owner")
        lines.append("    %s %s(%s)%s override { return %s.%s(%s); }" % (
            re.sub(r"\s+", " ", ret).strip(), name, ", ".join(decls), const.strip() and " const",
            mgr_expr, target or name, ", ".join(args)))
    return lines, owned


def wrapper(sdk, iface, mgr_expr, src):
    lines, owned = methods(sdk, iface, mgr_expr, src)
    print("%-16s %3d methods, %2d with the owner" % (iface, len(lines), owned))
    cls = iface + "ForPlugin"
    return "\n".join([
        "class %s final : public ISource2Mod%s" % (cls, iface),
        "{",
        "public:",
        "    explicit %s(PluginId owner) : m_owner(owner) {}" % cls,
        "",
        *lines,
        "",
        "private:",
        "    PluginId m_owner;",
        "};",
        "",
    ])


def main(sdk):
    out = ["// Generated by tools/pluginifaces/pluginifaces.py -- see that file.",
           "// Each plugin's own instance of the interfaces that care who calls.",
           '#include "core/plugins/pluginifaces.h"', ""]
    for iface, _, _, src in IFACES:
        out.append('#include "%s.h"' % src)
    out += ['#include "sourcepawn/natives.h"', "", "#include <cstring>", "", "namespace pluginifaces", "{", "namespace", "{"]
    for iface, _, expr, src in IFACES:
        out.append(wrapper(sdk, iface, expr, src))
    out += ["}", "", "void* Create(const char* iface, PluginId owner)", "{"]
    for iface, macro, _, _ in IFACES:
        out.append("    if (!strcmp(iface, SOURCE2MOD_%s_INTERFACE)) return static_cast<ISource2Mod%s*>(new %sForPlugin(owner));" % (macro, iface, iface))
    out += ["    if (!strcmp(iface, SOURCE2MOD_NATIVES_INTERFACE)) return sourcepawn::CreateNativesForPlugin(owner);",
            "    return nullptr;", "}", "}", ""]
    gen = os.path.join(ROOT, "src/core/plugins/gen")
    os.makedirs(gen, exist_ok=True)
    open(os.path.join(gen, "pluginifaces.gen.cpp"), "w", encoding="utf-8", newline="\n").write("\n".join(out))

    gen = os.path.join(ROOT, "src/sourcepawn/gen")
    os.makedirs(gen, exist_ok=True)
    open(os.path.join(gen, "nativesforplugin.gen.inc"), "w", encoding="utf-8", newline="\n").write(
        "// Generated by tools/pluginifaces/pluginifaces.py -- see that file.\n" + wrapper(sdk, *NATIVES[0:1], NATIVES[2], NATIVES[3]))


main(sys.argv[1] if len(sys.argv) > 1 else "/home/Source2Mod-sdk/public/source2mod")
