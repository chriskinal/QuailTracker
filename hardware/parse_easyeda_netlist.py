#!/usr/bin/env python3
"""
Parse EasyEDA schematic JSON to extract netlist information.

Reads the EasyEDA Pro/Std schematic export and extracts:
  - All components (designator, value, package, pin list)
  - All net names and their connected component pins
  - Connections via direct overlap, wires, and junctions

Output: a human-readable netlist summary organized by net name.
"""

import json
import sys
from collections import defaultdict

INPUT_FILE = "/Users/chris/Downloads/SCH_QuailTrack_v1_2026-02-15.json"
OUTPUT_FILE = "/Users/chris/Code/QuailTracker/hardware/easyeda_netlist_extract.txt"

EPSILON = 0.5  # coordinate matching tolerance


def parse_components(shapes):
    """Extract all LIB components with designator, value, package, and pins."""
    components = {}  # designator -> {value, package, pins: [{name, num, x, y}]}

    for s in shapes:
        if not s.startswith("LIB~"):
            continue
        # Skip the title block frame
        if "package`NONE`" in s:
            continue

        parts = s.split("#@$")

        # Extract designator and value from T (text) fields
        designator = ""
        value = ""
        for p in parts:
            if p.startswith("T~P~"):
                fields = p.split("~")
                for i, f in enumerate(fields):
                    if f == "comment" and i + 1 < len(fields):
                        designator = fields[i + 1]
                        break
            elif p.startswith("T~N~"):
                fields = p.split("~")
                for i, f in enumerate(fields):
                    if f == "comment" and i + 1 < len(fields):
                        value = fields[i + 1]
                        break

        # Extract package from header
        header = parts[0]
        package = ""
        if "package`" in header:
            package = header.split("package`")[1].split("`")[0]

        # Extract pins
        pin_list = []
        for p in parts:
            if not p.startswith("P~"):
                continue
            segs = p.split("^^")
            if len(segs) < 5:
                continue

            # Connection point coordinates from segment[1]
            cp = segs[1].split("~")
            px, py = float(cp[0]), float(cp[1])

            # Pin name from segment[3]: "1~x~y~rot~NAME~alignment..."
            name_fields = segs[3].split("~")
            pin_name = name_fields[4] if len(name_fields) > 4 else "?"

            # Pin number from segment[4]: "1~x~y~rot~NUM~alignment..."
            num_fields = segs[4].split("~")
            pin_num = num_fields[4] if len(num_fields) > 4 else "?"

            pin_list.append({
                "name": pin_name,
                "num": pin_num,
                "x": px,
                "y": py,
            })

        if designator:
            components[designator] = {
                "value": value,
                "package": package,
                "pins": pin_list,
            }

    return components


def parse_net_labels(shapes):
    """Extract all net label/port flags with their connection coordinates."""
    labels = []  # list of (net_name, x, y)

    for s in shapes:
        if not s.startswith("F~"):
            continue
        segs = s.split("^^")
        if len(segs) < 3:
            continue

        # Connection point from segment[1]
        cp = segs[1].split("~")
        x, y = float(cp[0]), float(cp[1])

        # Net name from segment[2] (first field before ~)
        net_name = segs[2].split("~")[0]

        labels.append((net_name, x, y))

    return labels


def parse_wires(shapes):
    """Extract all wire polylines as lists of (x, y) points."""
    wires = []

    for s in shapes:
        if not s.startswith("W~"):
            continue
        fields = s.split("~")
        coords_str = fields[1]
        coords = coords_str.split(" ")
        points = []
        for i in range(0, len(coords), 2):
            points.append((float(coords[i]), float(coords[i + 1])))
        wires.append(points)

    return wires


def parse_junctions(shapes):
    """Extract junction points (explicit wire join markers)."""
    junctions = []

    for s in shapes:
        if not s.startswith("J~"):
            continue
        fields = s.split("~")
        jx, jy = float(fields[1]), float(fields[2])
        junctions.append((jx, jy))

    return junctions


def points_match(p1, p2, eps=EPSILON):
    """Check if two (x, y) points are within epsilon of each other."""
    return abs(p1[0] - p2[0]) < eps and abs(p1[1] - p2[1]) < eps


def point_on_wire_segment(pt, seg_start, seg_end, eps=EPSILON):
    """Check if a point lies on a wire segment (horizontal or vertical lines)."""
    x, y = pt
    x1, y1 = seg_start
    x2, y2 = seg_end

    # Horizontal segment
    if abs(y1 - y2) < eps and abs(y - y1) < eps:
        min_x = min(x1, x2)
        max_x = max(x1, x2)
        if min_x - eps <= x <= max_x + eps:
            return True

    # Vertical segment
    if abs(x1 - x2) < eps and abs(x - x1) < eps:
        min_y = min(y1, y2)
        max_y = max(y1, y2)
        if min_y - eps <= y <= max_y + eps:
            return True

    return False


def build_netlist(components, net_labels, wires, junctions):
    """
    Build netlist by connecting net labels to component pins.

    Connection rules:
    1. Direct: net label and pin share the same coordinate
    2. Via wire: net label connects to a wire endpoint, and a pin connects
       to the same wire (at an endpoint or along a segment)
    3. Via wire chain: multiple wires joined at endpoints or junctions
    """
    # Collect all connection points: (type, identifier, x, y)
    # type: 'pin' or 'netlabel'
    # For pins: identifier = (designator, pin_name, pin_num)
    # For net labels: identifier = net_name

    # Build a coordinate-to-node mapping using union-find for net connectivity
    # Each unique coordinate that has a pin or net label is a node
    # Wires connect nodes at their endpoints and along their paths

    # Step 1: Collect all significant points
    pin_points = []  # (x, y, designator, pin_name, pin_num)
    for desig, comp in components.items():
        for pin in comp["pins"]:
            pin_points.append((pin["x"], pin["y"], desig, pin["name"], pin["num"]))

    label_points = []  # (x, y, net_name)
    for net_name, x, y in net_labels:
        label_points.append((x, y, net_name))

    # Step 2: Build connectivity graph
    # Each node is identified by its coordinate (rounded to avoid float issues)
    # We use union-find to group connected coordinates

    parent = {}

    def find(p):
        key = (round(p[0], 1), round(p[1], 1))
        if key not in parent:
            parent[key] = key
        while parent[key] != key:
            parent[key] = parent[parent[key]]
            key = parent[key]
        return key

    def union(p1, p2):
        r1 = find(p1)
        r2 = find(p2)
        if r1 != r2:
            parent[r1] = r2

    # Register all pin and label points
    for x, y, *_ in pin_points:
        find((x, y))
    for x, y, *_ in label_points:
        find((x, y))

    # Connect wire endpoints to each other
    for wire_pts in wires:
        for i in range(len(wire_pts) - 1):
            union(wire_pts[i], wire_pts[i + 1])

    # Connect pins and labels to wire endpoints/segments
    all_points = [(x, y) for x, y, *_ in pin_points] + [(x, y) for x, y, *_ in label_points]

    for pt in all_points:
        for wire_pts in wires:
            # Check endpoints
            for wp in wire_pts:
                if points_match(pt, wp):
                    union(pt, wp)
                    break
            # Check if point lies on any wire segment
            for i in range(len(wire_pts) - 1):
                if point_on_wire_segment(pt, wire_pts[i], wire_pts[i + 1]):
                    union(pt, wire_pts[i])
                    break

    # Connect via junctions (a junction means all wires/pins at that point are connected)
    for jpt in junctions:
        for pt in all_points:
            if points_match(pt, jpt):
                union(pt, jpt)
        for wire_pts in wires:
            for wp in wire_pts:
                if points_match(jpt, wp):
                    union(jpt, wp)

    # Step 3: Group by connected component
    # Map each root -> list of pins and net names
    root_to_pins = defaultdict(list)
    root_to_nets = defaultdict(set)

    for x, y, desig, pname, pnum in pin_points:
        root = find((x, y))
        root_to_pins[root].append((desig, pname, pnum))

    for x, y, net_name in label_points:
        root = find((x, y))
        root_to_nets[root].add(net_name)

    # Step 4: Build final netlist
    # Net name -> list of (designator, pin_name, pin_num)
    netlist = defaultdict(list)
    unnamed_nets = []

    all_roots = set(list(root_to_pins.keys()) + list(root_to_nets.keys()))
    unnamed_counter = 0

    for root in all_roots:
        pins_here = root_to_pins.get(root, [])
        nets_here = root_to_nets.get(root, set())

        if nets_here:
            # Use the net name(s)
            for net_name in nets_here:
                netlist[net_name].extend(pins_here)
        elif len(pins_here) > 1:
            # Multiple pins connected but no net label — unnamed net
            unnamed_counter += 1
            net_name = f"__unnamed_{unnamed_counter}__"
            netlist[net_name].extend(pins_here)

    # Deduplicate pins within each net
    for net_name in netlist:
        netlist[net_name] = sorted(set(netlist[net_name]))

    return dict(netlist)


def format_output(components, netlist):
    """Format the extraction results as readable text."""
    lines = []
    lines.append("=" * 72)
    lines.append("EasyEDA Schematic Netlist Extraction")
    lines.append(f"Source: {INPUT_FILE}")
    lines.append("=" * 72)
    lines.append("")

    # Section 1: Component List
    lines.append("-" * 72)
    lines.append("COMPONENT LIST")
    lines.append("-" * 72)
    lines.append("")

    # Sort components by designator type then number
    def sort_key(desig):
        prefix = "".join(c for c in desig if c.isalpha())
        num = "".join(c for c in desig if c.isdigit())
        return (prefix, int(num) if num else 0)

    for desig in sorted(components.keys(), key=sort_key):
        comp = components[desig]
        lines.append(f"  {desig:10s}  {comp['value']:30s}  [{comp['package']}]")
        pin_strs = []
        for pin in sorted(comp["pins"], key=lambda p: (int(p["num"]) if p["num"].isdigit() else 999, p["num"])):
            pin_strs.append(f"pin {pin['num']}={pin['name']}")
        if pin_strs:
            # Print pins in groups of 4
            for i in range(0, len(pin_strs), 6):
                chunk = ", ".join(pin_strs[i:i + 6])
                if i == 0:
                    lines.append(f"{'':12s}  Pins: {chunk}")
                else:
                    lines.append(f"{'':18s}{chunk}")
        lines.append("")

    # Section 2: Netlist by Net Name
    lines.append("-" * 72)
    lines.append("NETLIST BY NET NAME")
    lines.append("-" * 72)
    lines.append("")

    # Sort: named nets first (alphabetically), then unnamed
    named = sorted(n for n in netlist if not n.startswith("__"))
    unnamed = sorted(n for n in netlist if n.startswith("__"))

    for net_name in named + unnamed:
        pins = netlist[net_name]
        display_name = net_name if not net_name.startswith("__") else "(unnamed)"
        lines.append(f"  Net: {display_name}")
        lines.append(f"  {'─' * 40}")
        for desig, pname, pnum in pins:
            lines.append(f"    {desig:10s}  pin {pnum:4s}  ({pname})")
        lines.append("")

    # Section 3: Summary Statistics
    lines.append("-" * 72)
    lines.append("SUMMARY")
    lines.append("-" * 72)
    lines.append(f"  Total components:    {len(components)}")
    lines.append(f"  Total named nets:    {len(named)}")
    lines.append(f"  Total unnamed nets:  {len(unnamed)}")
    total_pins = sum(len(c["pins"]) for c in components.values())
    connected_pins = sum(len(v) for v in netlist.values())
    lines.append(f"  Total component pins: {total_pins}")
    lines.append(f"  Connected pin refs:   {connected_pins}")

    # Unconnected pins
    connected_set = set()
    for pins in netlist.values():
        for desig, pname, pnum in pins:
            connected_set.add((desig, pnum))

    unconnected = []
    for desig, comp in components.items():
        for pin in comp["pins"]:
            if (desig, pin["num"]) not in connected_set:
                unconnected.append((desig, pin["name"], pin["num"]))

    lines.append(f"  Unconnected pins:     {len(unconnected)}")
    if unconnected:
        lines.append("")
        lines.append("  Unconnected pin details:")
        for desig, pname, pnum in sorted(unconnected, key=lambda x: (sort_key(x[0]), x[2])):
            lines.append(f"    {desig:10s}  pin {pnum:4s}  ({pname})")

    lines.append("")
    return "\n".join(lines)


def main():
    # Load schematic
    print(f"Loading {INPUT_FILE}...")
    with open(INPUT_FILE) as f:
        data = json.load(f)

    shapes = data["schematics"][0]["dataStr"]["shape"]
    print(f"  Found {len(shapes)} shapes")

    # Parse elements
    components = parse_components(shapes)
    print(f"  Extracted {len(components)} components")

    net_labels = parse_net_labels(shapes)
    print(f"  Extracted {len(net_labels)} net labels")

    wires = parse_wires(shapes)
    print(f"  Extracted {len(wires)} wires")

    junctions = parse_junctions(shapes)
    print(f"  Extracted {len(junctions)} junctions")

    # Build netlist
    print("Building netlist...")
    netlist = build_netlist(components, net_labels, wires, junctions)
    print(f"  Found {len(netlist)} nets")

    # Format and write output
    output = format_output(components, netlist)

    with open(OUTPUT_FILE, "w") as f:
        f.write(output)
    print(f"\nOutput written to {OUTPUT_FILE}")

    # Also print to stdout
    print()
    print(output)


if __name__ == "__main__":
    main()
