#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Writes the per-aspect-ratio layout files for the games247 "Neon" theme. The retro TV in the
# game list keeps its 4:3 shape on every screen, so its size and everything below it depend on
# the screen's aspect ratio.
# Usage: python3 make_layouts.py <theme directory>
import sys

layouts = {"16-9": (1.7778, 0.40), "16-10": (1.6, 0.42), "4-3": (1.3333, 0.44),
           "21-9": (2.3333, 0.31)}

for name, (aspect, mw) in layouts.items():
    # The monitor bezel is 1000x750; its screen opening is x 7%-93%, y 6.7%-89.3%.
    mx, my = 0.515 + (0.47 - mw) / 2, 0.135
    mh = mw * aspect * 0.75
    sx, sw = mx + mw * 0.07, mw * 0.86
    sy, sh = my + mh * 0.067, mh * 0.826
    b = my + mh + 0.025  # top of the info panel
    elements = {
        ("system", "image", "sun"): {"size": f"{0.30 / aspect:.4f} 0.30"},
        ("gamelist", "image", "monitor"): {"pos": f"{mx:.4f} {my}", "size": f"{mw} {mh:.4f}"},
        ("gamelist", "image", "screenBlack"): {"pos": f"{sx:.4f} {sy:.4f}",
                                               "size": f"{sw:.4f} {sh:.4f}"},
        ("gamelist", "video", "gameVideo"): {"pos": f"{sx + sw / 2:.4f} {sy + sh / 2:.4f}",
                                             "maxSize": f"{sw:.4f} {sh:.4f}"},
        ("gamelist", "image", "infoPanel"): {"pos": f"0.515 {b:.4f}",
                                             "size": f"0.47 {0.91 - b:.4f}"},
        ("gamelist", "text", "gameName"): {"pos": f"0.53 {b + 0.015:.4f}"},
        ("gamelist", "datetime", "releaseYear"): {"pos": f"0.53 {b + 0.07:.4f}"},
        ("gamelist", "text", "developer"): {"pos": f"0.62 {b + 0.07:.4f}"},
        ("gamelist", "rating", "rating"): {"pos": f"0.97 {b + 0.085:.4f}"},
        ("gamelist", "text", "description"): {"pos": f"0.53 {b + 0.105:.4f}",
                                              "size": f"0.44 {0.895 - (b + 0.105):.4f}"},
    }
    lines = ["<theme>", '    <variant name="all">']
    for view in ("system", "gamelist"):
        lines.append(f'        <view name="{view}">')
        for (v, kind, elem), props in elements.items():
            if v != view:
                continue
            lines.append(f'            <{kind} name="{elem}">')
            lines += [f"                <{k}>{val}</{k}>" for k, val in props.items()]
            lines.append(f"            </{kind}>")
        lines.append("        </view>")
    lines += ["    </variant>", "</theme>", ""]
    with open(f"{sys.argv[1]}/aspect-ratio-{name}.xml", "w") as f:
        f.write("\n".join(lines))
