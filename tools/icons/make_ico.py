# Usage: python3 -I make_ico.py out.ico in16.png in24.png ...
# Packs PNG images into a Windows .ico (PNG-compressed entries, supported since Vista).
import struct, sys

out, pngs = sys.argv[1], sys.argv[2:]
datas = [open(p, "rb").read() for p in pngs]
header = struct.pack("<HHH", 0, 1, len(datas))
offset = 6 + 16 * len(datas)
entries = b""
for d in datas:
    w, h = struct.unpack(">II", d[16:24])  # PNG IHDR width/height
    entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(d), offset)
    offset += len(d)
with open(out, "wb") as f:
    f.write(header + entries + b"".join(datas))
