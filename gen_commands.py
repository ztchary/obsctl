import urllib.request
import json
import re

def parsetype(typ):
    typ = typ.replace("TBar", "Tbar") # bro why
    typ = typ.replace("Parameter", "Param")
    typ = typ.replace("KindList", "KindsList")
    matches = re.findall('[A-Z][a-z]*', typ)
    return [w.lower() for w in matches]

SRC = "https://raw.githubusercontent.com/obsproject/obs-websocket/refs/heads/master/docs/generated/protocol.json"

try:
    with urllib.request.urlopen(SRC) as response:
        protocol = json.loads(response.read().decode())
except:
    exit(1)

LEVELS = [
    [
        "scene", "record", "stream", "input", "output", "hotkey", "source",
        "profile", "virtual", "cam", "replay", "buffer", "custom", "event",
        "group", "canvas", "persistent", "data", "studio", "tbar", "video",
        "mix", "special", "inputs" ], [ "collection", "transition", "audio", "volume", "mute", "properties",
        "media", "filter", "service", "directory", "item", "deinterlace",
        "preview", "program", "filters", "interact", "projector"
    ], [
        "settings", "field", "order",
        "balance", "monitor", "sync", "tracks", "duration"
    ], [
        "get", "set", "toggle", "trigger", "list", "create", "remove", "start",
        "stop", "resume", "pause", "open", "save", "call", "broadcast", "send",
        "split", "duplicate", "sleep", "press", "default", "offset", "status",
        "current", "last", "vendor", "request", "enabled", "locked", "index",
        "blend", "version", "stats", "button", "action", "file", "name", "transform",
        "position", "override", "param", "cursor", "type", "screenshot",
        "caption", "dialog", "chapter", "active", "kinds", "items", "id"
    ]
]

FIXES = [
    ("get-list", "list"),
    ("scene-group", "group"),
    ("get-current-cursor", "get-cursor"),
]

EDGES = {
    "GetTransitionKindList": ["scene", "transition", "list-kinds"],
    "GetSceneItemSource": ["scene", "item", "get-source"]
}

tofix = []

def to_cmd(typ):
    if typ in EDGES:
        return EDGES[typ]
    typ = parsetype(typ)
    levels = []
    for level in LEVELS:
        rp = []
        for n in level:
            if n in typ:
                typ.remove(n)
                rp.append(n)
        levels.append(rp)
    joined = []
    for level in levels:
        if not level: continue
        t = "-".join(level)
        for s, r in FIXES:
            t = t.replace(s, r)
        joined.append(t)
    return joined

TYPES = ["ANY", "STRING", "NUMBER", "BOOLEAN", "OBJECT", "ARRAYSTRING", "ARRAYOBJECT"]

structs = f"""\
enum ParamType {{
    {",\n\t".join(TYPES)}
}};

const char *param_type_strs[] = {{
{"\n".join(f"\t[{t}] = \"{t}\"," for t in TYPES)}
}};

struct Param {{
    const char *name;
    const char *desc;
    enum ParamType type;
    int opt;
}};

struct Command {{
	const char *cmd;
    const char *desc;
    const char *type;
    const struct Param *param;
    const struct Param *resp;
    const struct Command **sub;
    int nparam;
    int nresp;
    int nsub;
}};
\n"""

TYPES = {
    "Any": 0,
    "String": 1,
    "Number": 2,
    "Boolean": 3,
    "Object": 4,
}

tree = {"desc": """\
OBS Websocket Commandline Utility.

Flags:
  [-h | --help]            : print help text for a subcommand
  [-s | --server] <server> : specify a server address (eg. ws://1.2.3.4:5678)
  [-p | --passwd] <passwd> : specify a password, if the server requires auth

Required arguments for subcommands are passed positionally.
Optional arguments are either passed positionally or
by 'key=value' pairs

If an argument contains '=', it must be passed as a 'key=value' pair

Server and password arguments can also be
passed through environment variables, which
makes scripting way easier. for example:

export OBSCTL_PASSWD=password1
obsctl record toggle"""}

for req in protocol["requests"]:
    typ = req["requestType"]
    desc = req["description"]
    cmd = to_cmd(typ)
    cur = tree
    for t in cmd:
        if "sub" not in cur:
            cur["sub"] = {}
        if t not in cur["sub"]:
            cur["sub"][t] = {}
        cur = cur["sub"][t]
    cur["desc"] = desc
    cur["typ"] = typ
    cur["param"] = []
    cur["resp"] = []
    
    for param in sorted(req["requestFields"], key=lambda x:x["valueOptional"]):
        name = json.dumps(param["valueName"])
        desc = json.dumps(param["valueDescription"])
        typ = param["valueType"].upper()
        opt = 1 if param["valueOptional"] else 0
        cur["param"].append(f"\t{{ {name}, {desc}, {typ}, {opt} }},")

    for param in req["responseFields"]:
        name = json.dumps(param["valueName"])
        desc = json.dumps(param["valueDescription"])
        typ = re.sub("[<>]", "", param["valueType"].upper())
        cur["resp"].append(f"\t{{ {name}, {desc}, {typ} }},")

c_lines = []

def sort_key(item):
    i = int("desc" not in item[1])
    return f"{i}{item[0]}"

def walk(node, id, cmd):
    child_ids = []
    if "sub" in node:
        for sub_id, sub_node in sorted(node["sub"].items(), key=sort_key):
            child_ids.append(walk(sub_node, f"{id}_{sub_id.replace('-', '_')}", sub_id))

    if child_ids:
        c_lines.append(f"static const struct Command *{id}_subcmd[] = {{\n\t&{"_cmd,\n\t&".join(child_ids)}_cmd\n}};\n")

    doparam = "param" in node and len(node["param"]) > 0
    if doparam:
        c_lines.append(f"static const struct Param {node["typ"]}_param[] = {{"); #}) bum ahh treesitter
        c_lines.extend(node["param"])
        c_lines.append(f"}};\n")

    doresp = "resp" in node and len(node["resp"]) > 0
    if doresp:
        c_lines.append(f"static const struct Param {node["typ"]}_resp[] = {{"); #})
        c_lines.extend(node["resp"])
        c_lines.append(f"}};\n")

    c_lines.extend([
        f"static const struct Command {id}_cmd = {{", # }
        f"\t{json.dumps(cmd)},",
        f"\t{json.dumps(node["desc"]) if "desc" in node else "NULL"},",
        f"\t{json.dumps(node["typ"]) if "typ" in node else "NULL"},",
        f"\t{node["typ"]}_param," if doparam else "\tNULL,",
        f"\t{node["typ"]}_resp," if doresp else "\tNULL,",
        f"\t{id}_subcmd," if "sub" in node else "\tNULL,",
        f"\t{len(node["param"]) if doparam else 0},",
        f"\t{len(node["resp"]) if doresp else 0},",
        f"\t{len(child_ids)}",
        f"}};\n"
    ])
    return id

walk(tree, "obsctl", "obsctl")

with open("commands.h", "w") as f:
    f.write(structs)
    f.write("\n".join(c_lines))

