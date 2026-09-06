import json
import re

def parsetype(typ):
    typ = typ.replace("TBar", "Tbar") # bro why
    typ = typ.replace("Parameter", "Param")
    typ = typ.replace("KindList", "KindsList")
    matches = re.findall('[A-Z][a-z]*', typ)
    return [w.lower() for w in matches]

with open("protocol.json") as f:
    protocol = json.load(f)

LEVELS = [
    [
        "scene", "record", "stream", "input", "output", "hotkey", "source",
        "profile", "virtual", "cam", "replay", "buffer", "custom", "event",
        "group", "canvas", "persistent", "data", "studio", "tbar", "video",
        "mix", "special", "inputs" ], [ "collection", "transition", "audio", "volume", "mute", "properties",
        "media", "filter", "service", "directory", "item", "deinterlace",
        "preview", "program", "filters", "interact", "projector"
    ], [
        "settings", "transform", "field", "order",
        "balance", "monitor", "sync", "tracks", "duration"
    ], [
        "get", "set", "toggle", "trigger", "list", "create", "remove", "start",
        "stop", "resume", "pause", "open", "save", "call", "broadcast", "send",
        "split", "duplicate", "sleep", "press", "default", "offset", "status",
        "current", "last", "vendor", "request", "enabled", "locked", "index",
        "blend", "version", "stats", "button", "action", "file", "name",
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

structs = """\
struct Param {
    const char *name;
    const char *desc;
    int type;
    int opt;
};

struct Command {
	const char *cmd;
    const char *desc;
    const char *type;
    const struct Param *param;
    const struct Command *sub;
    int nparam;
    int nsub;
};
\n"""

tree = {}
TYPES = {
    "Any": 0,
    "String": 1,
    "Number": 2,
    "Boolean": 3,
    "Object": 4,
}

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
    
    for param in req["requestFields"]:
        name = json.dumps(param["valueName"])
        desc = json.dumps(param["valueDescription"])
        typ = TYPES[param["valueType"]]
        opt = 1 if param["valueOptional"] else 0
        cur["param"].append(f"\t{{ {name}, {desc}, {typ}, {opt} }},")

c_lines = []

def walk(node, id, cmd):
    child_ids = []
    if "sub" in node:
        for sub_id, sub_node in node["sub"].items():
            child_ids.append(walk(sub_node, f"{id}_{sub_id.replace('-', '_')}", sub_id))

    if child_ids:
        c_lines.append(f"static const struct Command {id}_subcmd[] = {{\n\t{"_cmd,\n\t".join(child_ids)}_cmd\n}};\n")

    doparam = "param" in node and len(node["param"]) > 0
    if doparam:
        c_lines.append(f"static const struct Param {node["typ"]}_param[] = {{"); #}) bum ahh treesitter
        c_lines.extend(node["param"])
        c_lines.append(f"}};\n")
        

    c_lines.extend([
        f"static const struct Command {id}_cmd = {{", # }
        f"\t{json.dumps(cmd)},",
        f"\t{json.dumps(node["desc"]) if "desc" in node else "NULL"},",
        f"\t{json.dumps(node["typ"]) if "desc" in node else "NULL"},",
        f"\t{node["typ"]}_param," if doparam else "\tNULL,",
        f"\t{id}_subcmd," if "sub" in node else "\tNULL,",
        f"\t{len(node["param"]) if "param" in node else 0},",
        f"\t{len(child_ids)}",
        f"}};\n"
    ])
    return id

walk(tree, "obsctl", "obsctl")

with open("commands.h", "w") as f:
    f.write(structs)
    f.write("\n".join(c_lines))

