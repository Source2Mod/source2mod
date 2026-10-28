#!/usr/bin/env python3
"""
Older revisions of the Source2Mod interfaces, served by the current core.

A plugin sees one class per interface (ISource2ModCommands) and asks the core
for the revision it was built with ("ISource2ModCommands004"). When a revision
changes the vtable -- a method inserted, moved, removed or changed -- plugins
built against the old one would call the wrong slots. Instead of copying the
old methods into the core, this tool:

  bump <ISource2ModX ...>   freezes the current revision of each interface
                            into src/compat/<ISource2ModX00N>.h (its virtuals,
                            in their order) and raises the revision in the
                            SDK header. Run it BEFORE editing the interface.
  snapshot <ISource2ModX ...>
                            the freeze alone, without touching the SDK.
  generate                  writes src/compat/gen/: for every frozen revision
                            an adapter class whose vtable is the old one and
                            whose methods forward, by name and signature, to
                            the current interface; plus the registry the
                            factory serves them from.
  check                     generate into memory and fail if src/compat/gen/
                            differs (CI).

A frozen method with no match in the current interface (its signature
changed, or it was removed) is only declared in the adapter; write its body
in src/compat/<ISource2ModX00N>.cpp -- the link fails until it exists.

The SDK is found through $SOURCE2MOD_SDK, else ../Source2Mod-sdk.
"""

import os
import re
import sys

CORE_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
COMPAT_DIR = os.path.join(CORE_DIR, "src", "compat")
GEN_DIR = os.path.join(COMPAT_DIR, "gen")


def sdk_dir():
    d = os.environ.get("SOURCE2MOD_SDK") or os.path.join(CORE_DIR, "..", "Source2Mod-sdk")
    return os.path.normpath(d)


def sdk_header(iface):
    return os.path.join(sdk_dir(), "public", "source2mod", iface + ".h")


class ParseError(Exception):
    pass


# --------------------------------------------------------------------------
# C++ declaration parsing -- enough for the interface headers, which are
# plain lists of virtuals. Anything it cannot read is an error, not a guess.
# --------------------------------------------------------------------------

def strip_comments(text):
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            out.append("\n" * text.count("\n", i, n if j < 0 else j))
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def match_brace(text, open_pos):
    pairs = {"{": "}", "(": ")", "[": "]", "<": ">"}
    opener = text[open_pos]
    closer = pairs[opener]
    depth = 0
    i = open_pos
    while i < len(text):
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < len(text) and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == opener:
            depth += 1
        elif c == closer:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ParseError("unbalanced %s at %d" % (opener, open_pos))


def class_body(text, name):
    for m in re.finditer(r"\b(?:class|struct)\s+" + re.escape(name) + r"\b", text):
        i = m.end()
        # skip "final" and the base clause up to the body; a ';' first is a forward declaration
        while i < len(text) and text[i] not in "{;":
            i += 1
        if i < len(text) and text[i] == "{":
            end = match_brace(text, i)
            return text[i + 1:end]
    raise ParseError("class %s not found" % name)


def statements(body):
    """Top-level statements of a class body, each with any function body or nested type body kept."""
    out, cur, i = [], [], 0
    while i < len(body):
        c = body[i]
        if c in "\"'":
            j = i + 1
            while j < len(body) and body[j] != c:
                j += 2 if body[j] == "\\" else 1
            cur.append(body[i:j + 1])
            i = j + 1
            continue
        if c == "(":
            j = match_brace(body, i)
            cur.append(body[i:j + 1])
            i = j + 1
            continue
        if c == "{":
            j = match_brace(body, i)
            cur.append(body[i:j + 1])
            i = j + 1
            k = i
            while k < len(body) and body[k].isspace():
                k += 1
            if k < len(body) and body[k] == ";":
                cur.append(";")
                i = k + 1
            out.append("".join(cur))
            cur = []
            continue
        if c == ";":
            cur.append(";")
            out.append("".join(cur))
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    rest = "".join(cur).strip()
    if rest:
        out.append(rest)
    return [s for s in (re.sub(r"\s+", " ", s).strip() for s in out) if s]


def split_top(text, sep=","):
    parts, depth, cur, i = [], 0, [], 0
    while i < len(text):
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < len(text) and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c in "(<[{":
            depth += 1
        elif c in ")>]}":
            depth -= 1
        if c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    parts.append("".join(cur))
    return [p.strip() for p in parts]


def norm_type(t):
    t = re.sub(r"\s+", " ", t.strip())
    t = re.sub(r"\s*([*&<>,:()\[\]])\s*", r"\1", t)
    return t


QUALIFIER_WORDS = {"const", "volatile", "struct", "class", "enum", "union"}
TYPE_WORDS = QUALIFIER_WORDS | {"unsigned", "signed", "int", "char", "bool", "float", "double", "void", "long", "short", "auto"}


def split_param(p):
    """'const char* name = nullptr' -> ('const char*', 'name'); a type alone -> (type, None)."""
    p = split_top(p, "=")[0].strip()
    if not p:
        return None
    m = re.match(r"^(.*?[\s*&>])([A-Za-z_]\w*)$", p)
    if m and m.group(2) not in TYPE_WORDS:
        head = m.group(1).strip()
        head_words = re.findall(r"[A-Za-z_]\w*", head)
        # "const Foo" alone is a type, not "const" named Foo
        if head and not (all(w in QUALIFIER_WORDS for w in head_words) and not re.search(r"[*&>]$", head)):
            return head, m.group(2)
    return p, None


class Method:
    def __init__(self, ret, name, params, quals, text):
        self.ret = ret
        self.name = name
        self.params = params          # [(type, name-or-None)]
        self.quals = quals            # "const" or ""
        self.text = text

    @property
    def is_dtor(self):
        return self.name.startswith("~")

    def key(self):
        return (self.name, tuple(norm_type(t) for t, _ in self.params), self.quals)

    def decl(self, cls, pure):
        params = ", ".join("%s a%d" % (t, i) for i, (t, _) in enumerate(self.params))
        q = (" " + self.quals) if self.quals else ""
        if self.is_dtor:
            return "virtual ~%s() = default;" % cls
        return "virtual %s %s(%s)%s%s;" % (self.ret, self.name, params, q, " = 0" if pure else "")


def parse_virtual(stmt):
    s = stmt[len("virtual"):].strip()
    s = re.sub(r";$", "", s).strip()
    # drop an inline body, "= 0", "= default", override/final
    paren = s.find("(")
    if paren < 0:
        raise ParseError("no parameter list: " + stmt)
    close = match_brace(s, paren)
    tail = s[close + 1:]
    brace = tail.find("{")
    if brace >= 0:
        tail = tail[:brace]
    tail = re.sub(r"=\s*(0|default|delete)", "", tail)
    quals = "const" if re.search(r"\bconst\b", tail) else ""
    head = s[:paren].strip()
    m = re.match(r"^(.*?)\s*(~?[A-Za-z_]\w*)$", head)
    if not m:
        raise ParseError("cannot read: " + stmt)
    ret, name = m.group(1).strip(), m.group(2)
    params_text = s[paren + 1:close].strip()
    params = []
    if params_text and params_text != "void":
        for p in split_top(params_text):
            sp = split_param(p)
            if sp is None:
                raise ParseError("empty parameter in: " + stmt)
            params.append(sp)
    return Method(ret, name, params, quals, stmt)


NESTED_RE = re.compile(r"^(?:enum(?:\s+(?:class|struct))?|struct|class|union)\s+([A-Za-z_]\w*)")


def parse_interface(text, cls):
    body = class_body(strip_comments(text), cls)
    methods, nested = [], []
    for st in statements(body):
        st = re.sub(r"^(?:(?:public|protected|private)\s*:\s*)+", "", st).strip()
        if not st:
            continue
        if st.startswith("virtual "):
            methods.append(parse_virtual(st))
            continue
        m = NESTED_RE.match(st)
        if m:
            nested.append(m.group(1))
            continue
        m = re.match(r"^using\s+([A-Za-z_]\w*)\s*=", st)
        if m:
            nested.append(m.group(1))
            continue
        if st.startswith("typedef "):
            m = re.search(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)", st) or re.search(r"([A-Za-z_]\w*)\s*;$", st)
            if m:
                nested.append(m.group(1))
    return methods, nested


def interface_version(text, cls):
    m = re.search(r'#define\s+(\w+)\s+"' + re.escape(cls) + r'(\d{3})"', text)
    if not m:
        raise ParseError("no #define \"%sNNN\" in the SDK header" % cls)
    return m.group(1), int(m.group(2)), m


# --------------------------------------------------------------------------
# snapshot / bump
# --------------------------------------------------------------------------

HEADER_NOTE = "// Generated by tools/ifacecompat/ifacecompat.py -- see that file.\n"


def snapshot(cls):
    path = sdk_header(cls)
    text = open(path, encoding="utf-8").read()
    _, rev, _ = interface_version(text, cls)
    methods, nested = parse_interface(text, cls)
    frozen = "%s%03d" % (cls, rev)
    out = os.path.join(COMPAT_DIR, frozen + ".h")
    if os.path.exists(out):
        raise ParseError("%s is frozen already: %s" % (frozen, out))
    lines = [
        "// %s as the core served it at revision %03d: its virtuals, in their order." % (cls, rev),
        "// Frozen -- edit only to keep it compiling (an SDK type it names was renamed or removed).",
        HEADER_NOTE.rstrip(),
        "#pragma once",
        "",
        "#include <source2mod/%s.h>" % cls,
        "",
        "namespace compat",
        "{",
        "class %s" % frozen,
        "{",
        "public:",
    ]
    for n in nested:
        lines.append("    using %s = ::%s::%s;" % (n, cls, n))
    if nested:
        lines.append("")
    for m in methods:
        lines.append("    " + m.decl(frozen, pure=True))
    lines += ["};", "}", ""]
    os.makedirs(COMPAT_DIR, exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))
    print("froze %s (%d virtuals) -> %s" % (frozen, len(methods), os.path.relpath(out, CORE_DIR)))
    return rev


def bump(cls):
    rev = snapshot(cls)
    path = sdk_header(cls)
    text = open(path, encoding="utf-8", newline="").read()
    _, _, m = interface_version(text, cls)
    new = '#define %s "%s%03d"' % (m.group(1), cls, rev + 1)
    text = text[:m.start()] + new + text[m.end():]
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)
    print("%s: %03d -> %03d in %s" % (cls, rev, rev + 1, os.path.relpath(path, sdk_dir())))


# --------------------------------------------------------------------------
# generate
# --------------------------------------------------------------------------

def frozen_list():
    if not os.path.isdir(COMPAT_DIR):
        return []
    out = []
    for f in sorted(os.listdir(COMPAT_DIR)):
        m = re.match(r"^(ISource2Mod\w+?)(\d{3})\.h$", f)
        if m:
            out.append((m.group(1), int(m.group(2)), f))
    return out


def build_adapter(cls, rev, fname):
    frozen = "%s%03d" % (cls, rev)
    adapter = frozen + "Adapter"
    old_methods, _ = parse_interface(open(os.path.join(COMPAT_DIR, fname), encoding="utf-8").read(), frozen)
    sdk_text = open(sdk_header(cls), encoding="utf-8").read()
    define, current_rev, _ = interface_version(sdk_text, cls)
    if current_rev <= rev:
        raise ParseError("%s is not older than the SDK's %s%03d -- frozen before the bump?" % (frozen, cls, current_rev))
    current = {m.key(): m for m in parse_interface(sdk_text, cls)[0] if not m.is_dtor}

    body, manual = [], []
    for m in old_methods:
        if m.is_dtor:
            continue
        params = ", ".join("%s a%d" % (t, i) for i, (t, _) in enumerate(m.params))
        q = (" " + m.quals) if m.quals else ""
        cur = current.get(m.key())
        if cur is None or norm_type(cur.ret) != norm_type(m.ret):
            body.append("    %s %s(%s)%s override;   // hand-written, src/compat/%s.cpp" % (m.ret, m.name, params, q, frozen))
            manual.append(m.name)
            continue
        # std::forward without <utility>: a by-value parameter moves on, a reference stays one
        args = ", ".join("static_cast<decltype(a%d)&&>(a%d)" % (i, i) for i in range(len(m.params)))
        body.append("    %s %s(%s)%s override { return m_pCurrent->%s(%s); }" % (m.ret, m.name, params, q, m.name, args))

    src = [
        HEADER_NOTE.rstrip(),
        "// %s for plugins built against it: the old vtable, forwarding to the current %s." % (frozen, cls),
        '#include "compat/%s"' % fname,
        '#include "compat/compat.h"',
        "",
        "namespace compat",
        "{",
        "class %s final : public %s" % (adapter, frozen),
        "{",
        "public:",
        "    ::%s* m_pCurrent = nullptr;" % cls,
        "",
    ] + body + [
        "};",
        "",
        "void* Get%s(CurrentInterfaceFn current)" % frozen,
        "{",
        "    static %s s_adapter;" % adapter,
        "    if (!s_adapter.m_pCurrent)",
        "        s_adapter.m_pCurrent = static_cast<::%s*>(current(%s));" % (cls, define),
        "    return s_adapter.m_pCurrent ? static_cast<%s*>(&s_adapter) : nullptr;" % frozen,
        "}",
        "}",
        "",
    ]
    return frozen, "\n".join(src), manual


def generated_files():
    files, entries, notes = {}, [], []
    for cls, rev, fname in frozen_list():
        frozen, src, manual = build_adapter(cls, rev, fname)
        files[frozen + ".gen.cpp"] = src
        entries.append(frozen)
        if manual:
            notes.append("%s: write %s in src/compat/%s.cpp" % (frozen, ", ".join(manual), frozen))
    reg = [
        HEADER_NOTE.rstrip(),
        "// Every frozen revision the core serves, see Source2ModFactory().",
        '#include "compat/compat.h"',
        "",
        "namespace compat",
        "{",
    ]
    reg += ["void* Get%s(CurrentInterfaceFn current);" % e for e in entries]
    reg += [
        "",
        "const FrozenInterface* FrozenInterfaces(size_t& count)",
        "{",
    ]
    if entries:
        reg.append("    static const FrozenInterface s_frozen[] = {")
        reg += ['        { "%s", &Get%s },' % (e, e) for e in entries]
        reg += ["    };", "    count = sizeof(s_frozen) / sizeof(s_frozen[0]);", "    return s_frozen;"]
    else:
        reg += ["    count = 0;", "    return nullptr;"]
    reg += ["}", "}", ""]
    files["registry.gen.cpp"] = "\n".join(reg)
    return files, notes


def generate():
    files, notes = generated_files()
    os.makedirs(GEN_DIR, exist_ok=True)
    for f in os.listdir(GEN_DIR):
        if f.endswith(".gen.cpp") and f not in files:
            os.remove(os.path.join(GEN_DIR, f))
            print("removed gen/%s" % f)
    for name, src in files.items():
        with open(os.path.join(GEN_DIR, name), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(src)
    print("generated %d adapter(s) into src/compat/gen" % (len(files) - 1))
    for n in notes:
        print("  hand-written: " + n)


def check():
    files, _ = generated_files()
    bad = []
    on_disk = set(f for f in os.listdir(GEN_DIR) if f.endswith(".gen.cpp")) if os.path.isdir(GEN_DIR) else set()
    for name, src in files.items():
        p = os.path.join(GEN_DIR, name)
        if not os.path.exists(p) or open(p, encoding="utf-8").read() != src:
            bad.append(name)
    bad += sorted(on_disk - set(files))
    if bad:
        print("src/compat/gen is stale (%s): run tools/ifacecompat/ifacecompat.py generate" % ", ".join(bad))
        return 1
    print("src/compat/gen is up to date")
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]
    try:
        if cmd == "snapshot":
            for a in args:
                snapshot(a)
        elif cmd == "bump":
            for a in args:
                bump(a)
        elif cmd == "generate":
            generate()
        elif cmd == "check":
            return check()
        else:
            print(__doc__)
            return 2
    except ParseError as e:
        print("ifacecompat: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
