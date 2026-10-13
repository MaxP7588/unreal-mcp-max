"""Turn a blueprint graph dump into readable pseudocode."""

import json
import sys
from pathlib import Path

ENTRY_CLASSES = {
    "K2Node_Event",
    "K2Node_CustomEvent",
    "K2Node_FunctionEntry",
    "K2Node_InputAction",
    "K2Node_ComponentBoundEvent",
    "K2Node_EnhancedInputAction",
    "K2Node_InputKey",
    "K2Node_Timeline",
}

SKIP_ARG_NAMES = {"self", "WorldContextObject", "LatentInfo", "__WorldContext"}
SKIP_DEFAULT_NAMES = {
    "bPrintToScreen",
    "bPrintToLog",
    "Duration",
    "Key",
    "TextColor",
}
EMPTY_DEFAULTS = {"", "None", "0", "0.0", "0.000000", "false", "False"}


def load_dump(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def short_title(node):
    if node.get("component") and node.get("delegate"):
        return node["component"] + "." + node["delegate"]
    if node.get("custom_event"):
        return node["custom_event"]
    if node.get("function"):
        return node["function"]
    if node.get("variable"):
        prefix = "Set " if node.get("class") == "K2Node_VariableSet" else "Get "
        return prefix + node["variable"]
    if node.get("cast_target"):
        return "Cast To " + node["cast_target"]
    if node.get("macro"):
        return "Macro " + node["macro"]
    if node.get("timeline"):
        return "Timeline " + node["timeline"]
    if node.get("event"):
        return node["event"]
    if node.get("collapsed_graph"):
        return "Collapsed " + node["collapsed_graph"]
    return node.get("title", "?").split("\n")[0]


def pin_default(pin_info):
    label = pin_info.get("default_label")
    if label:
        return str(label)
    default = pin_info.get("default_object") or pin_info.get("default", "")
    return str(default)


def expr(node_id, pin, nodes, incoming, depth=0):
    node = nodes[node_id]
    title = short_title(node)
    if depth > 4:
        return title

    args = []
    for pin_info in node.get("pins", []):
        if pin_info.get("direction") != "in" or pin_info.get("type") == "exec":
            continue
        name = pin_info.get("name", "")
        source = incoming.get((node_id, name))
        if name in SKIP_ARG_NAMES:
            if source and name == "self":
                args.append("self=" + expr(source[0], source[1], nodes, incoming, depth + 1))
            continue
        if source:
            args.append(name + "=" + expr(source[0], source[1], nodes, incoming, depth + 1))
        else:
            default = pin_default(pin_info)
            if name not in SKIP_DEFAULT_NAMES and default not in EMPTY_DEFAULTS:
                args.append(name + "=" + default[:80])

    if node.get("class") == "K2Node_VariableGet":
        return "$" + (node.get("variable") or title.replace("Get ", "").replace("Obtener ", ""))
    if node.get("class") == "K2Node_Knot":
        return args[0].split("=", 1)[1] if args else "?"
    if node.get("class") == "K2Node_MakeArray":
        values = [arg.split("=", 1)[1] for arg in args]
        return "[" + ", ".join(values) + "]"

    rendered = title + "(" + ", ".join(args) + ")"
    if pin not in ("ReturnValue", "Output_Get", "OutputPin"):
        rendered += "." + pin
    return rendered


def comment_index(graph):
    indexed = {}
    for comment in graph.get("comments") or []:
        text = (comment.get("text") or "").replace("\n", " ").strip()
        if not text:
            continue
        for node_id in comment.get("nodes") or []:
            indexed.setdefault(node_id, []).append(text)
    return indexed


def render_graph(graph):
    nodes = {node["id"]: node for node in graph.get("nodes") or []}
    outgoing = {}
    incoming = {}
    for link in graph.get("links") or []:
        outgoing.setdefault((link["from_node"], link["from_pin"]), []).append(
            (link["to_node"], link["to_pin"])
        )
        incoming[(link["to_node"], link["to_pin"])] = (link["from_node"], link["from_pin"])

    comments = comment_index(graph)
    entries = [node for node in graph.get("nodes") or [] if node.get("class") in ENTRY_CLASSES]
    seen = set()
    lines = []

    def annotate(node_id, text):
        boxes = comments.get(node_id)
        if boxes:
            text += "  // [" + " | ".join(boxes) + "]"
        return text

    def append_bound(node, indent):
        bound = node.get("bound_graph")
        if not isinstance(bound, dict) or not bound.get("nodes"):
            return
        pad = "  " * indent
        lines.append(pad + "{ collapsed " + bound.get("name", "") + " }")
        for subline in render_graph(bound):
            lines.append(pad + "  " + subline)

    def walk(node_id, indent):
        node = nodes.get(node_id)
        if node is None:
            lines.append("  " * indent + "-> (nodo fuera de esta página) " + node_id)
            return
        title = short_title(node)
        marker = ""
        if node.get("enabled") == "Disabled":
            marker = " [DESACTIVADO]"
        elif node.get("enabled") == "DevelopmentOnly":
            marker = " [SOLO DESARROLLO]"
        if node_id in seen:
            lines.append("  " * indent + "-> (salta a) " + title)
            return
        seen.add(node_id)
        if node.get("class") not in ENTRY_CLASSES:
            text = expr(node_id, "ReturnValue", nodes, incoming)
            for suffix in (".ReturnValue", ".Output_Get", ".OutputPin"):
                if text.endswith(suffix):
                    text = text[: -len(suffix)]
            if node.get("comment"):
                text += "  // " + node["comment"].replace("\n", " ")
            lines.append(annotate(node_id, "  " * indent + text[:700] + marker))
            append_bound(node, indent + 1)
        exec_pins = [
            pin["name"]
            for pin in node.get("pins") or []
            if pin.get("direction") == "out" and pin.get("type") == "exec"
        ]
        for pin_name in exec_pins:
            targets = outgoing.get((node_id, pin_name), [])
            if len(exec_pins) > 1:
                lines.append("  " * (indent + 1) + "[" + pin_name + "]:")
                if not targets:
                    lines.append("  " * (indent + 2) + "(sin cable)")
            for target_id, _target_pin in targets:
                walk(target_id, indent + (2 if len(exec_pins) > 1 else 0))

    for entry in entries:
        params = [
            pin["name"]
            for pin in entry.get("pins") or []
            if pin.get("direction") == "out" and pin.get("type") not in ("exec", "delegate")
        ]
        comment = ("  // " + entry["comment"].replace("\n", " ")) if entry.get("comment") else ""
        exec_outs = [
            pin["name"]
            for pin in entry.get("pins") or []
            if pin.get("direction") == "out" and pin.get("type") == "exec"
        ]
        unlinked = exec_outs and not any(outgoing.get((entry["id"], pin_name)) for pin_name in exec_outs)
        loose = "  [sin cable de salida]" if unlinked else ""
        lines.append("")
        lines.append(annotate(entry["id"], "== " + short_title(entry) + "(" + ", ".join(params) + ")" + comment + loose))
        walk(entry["id"], 1)

    orphans = [
        node
        for node in graph.get("nodes") or []
        if node["id"] not in seen and any(pin.get("type") == "exec" for pin in node.get("pins") or [])
    ]
    roots = [node for node in orphans if not incoming.get((node["id"], "execute"))]
    if roots:
        lines.append("")
        lines.append("!! exec sin evento de entrada:")
        for root in roots:
            walk(root["id"], 1)

    return lines


def render_dump(dump):
    lines = ["##### " + dump.get("blueprint", "?") + "  parent=" + dump.get("parent", "")]
    variables = []
    for var in dump.get("variables") or []:
        rendered = var["name"] + ":" + var.get("type", "")
        if var.get("sub_type"):
            rendered += "/" + var["sub_type"]
        if var.get("container") == "array":
            rendered += "[]"
        if var.get("default_label"):
            rendered += "=" + var["default_label"]
        variables.append(rendered)
    if variables:
        lines.append("vars: " + ", ".join(variables))
    for graph in dump.get("graphs") or []:
        lines.append("")
        lines.append("## [" + graph.get("name", "?") + "]")
        lines.extend(render_graph(graph))
    return lines


def main(argv):
    markdown_path = None
    paths = []
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--markdown":
            markdown_path = argv[index + 1]
            index += 2
            continue
        paths.append(arg)
        index += 1

    if not paths:
        print(__doc__)
        return 1

    sections = []
    for path in paths:
        dump = load_dump(path)
        sections.append("\n".join(render_dump(dump)))

    text = "\n\n".join(sections) + "\n"
    if markdown_path:
        Path(markdown_path).write_text(text, encoding="utf-8")
        print("wrote " + markdown_path)
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
