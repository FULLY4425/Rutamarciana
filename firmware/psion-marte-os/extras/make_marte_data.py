"""Generate the watch's centimetre-quantized Glen Torridon map from this repository.
Run: python firmware/psion-marte-os/extras/make_marte_data.py [repository root]
Reads assets/glen-torridon/{dem.npy,routes.json,targets.json}. Keeps the supplied
Adafruit font. NASA facts source: https://science.nasa.gov/mars/facts/
No other project's dataset is read. Requires NumPy, already in this repository.
"""
import json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.dirname(HERE)
DEFAULT = str(__import__('pathlib').Path(__file__).resolve().parents[3])
PREFER = ("Stornoway", "Whaligoe")
MEMBERS = {"Jura": 1, "KfH": 2, "Glasgow": 4, "KH-GG transition": 8}


def grid_point(t, p):                       # the same as mars/planner.py
    return (math.floor((p["lon"] - t["west"]) / t["degreeStep"] + .5), math.floor((t["north"] - p["lat"]) / t["degreeStep"] + .5))


def pack_terrain(u, w, h):
    out = bytearray()
    for y in range(h):
        for x in range(w):
            v = u[y * w + x]
            if x == 0:
                g = u[(y - 1) * w] if y else 0
            elif y == 0:
                g = u[x - 1]
            else:
                g = u[y * w + x - 1] + u[(y - 1) * w + x] - u[(y - 1) * w + x - 1]
            r = v - g
            if -127 <= r <= 127:
                out.append(r & 0xFF)
            else:
                out += bytes((0x80, v >> 8, v & 0xFF))
    return bytes(out)


def unpack_terrain(b, w, h):                # (checks the packer; the firmware's mrUnpack() does the same)
    u, i = [0] * (w * h), 0
    for y in range(h):
        for x in range(w):
            c = b[i]; i += 1
            if c == 0x80:
                u[y * w + x] = (b[i] << 8) | b[i + 1]; i += 2
                continue
            r = c - 256 if c > 127 else c
            if x == 0:
                g = u[(y - 1) * w] if y else 0
            elif y == 0:
                g = u[x - 1]
            else:
                g = u[y * w + x - 1] + u[(y - 1) * w + x] - u[(y - 1) * w + x - 1]
            u[y * w + x] = g + r
    return u


def cp437(s):                               # Spanish text -> the font's CP437 bytes
    for a, b in (("₂", "2"), ("−", "-"), ("–", "-"), ("—", "-")):
        s = s.replace(a, b)
    return s.encode("cp437", errors="replace")


def c_bytes(b):                             # bytes -> a C string literal (\xNN; split where a hex digit follows one)
    out, after_hex = '"', False
    for ch in b:
        c = chr(ch)
        if ch >= 0x80 or c in '"\\':
            out += "\\x%02X" % ch
            after_hex = True
            continue
        if after_hex and c in "0123456789abcdefABCDEF":
            out += '" "'
        out += c
        after_hex = False
    return out + '"'


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    data = os.path.join(src, "assets", "glen-torridon")
    import numpy as np
    meta = json.load(open(os.path.join(data, "routes.json"), encoding="utf-8"))
    dem = np.load(os.path.join(data, "dem.npy"), allow_pickle=False)
    assert np.isfinite(dem).all(), "This offline format requires finite elevations"
    t = dict(width=meta['grid']['cols'], height=meta['grid']['rows'], elevations=dem.ravel().tolist(),
             cellMeters=meta['cell_m'], cellXMeters=meta['cell_x_m'], west=meta['spatial']['west_deg'],
             north=meta['spatial']['north_deg'], degreeStep=meta['spatial']['degree_step'])
    targets = json.load(open(os.path.join(data, "targets.json"), encoding="utf-8"))
    w, h = t["width"], t["height"]
    cm = [round(v * 100) for v in t["elevations"]]
    assert all(abs(c / 100 - v) <= .00501 for c, v in zip(cm, t["elevations"])), "centimetre quantization exceeded"
    base = min(cm)
    u = [c - base for c in cm]
    assert max(u) < 1 << 14, "height range needs more than 14 bits"
    packed = pack_terrain(u, w, h)
    assert unpack_terrain(packed, w, h) == u

    sites = {}
    for p in targets:
        sites.setdefault(grid_point(t, p), []).append(p)
    recs = []
    for (x, y), lst in sites.items():
        assert 0 <= x < w and 0 <= y < h
        named = [p for p in lst if not p["name"].startswith("aegis_")] or lst   # (AEGIS = targets the rover picked itself)
        first = min(named, key=lambda p: p["sol"])       # (min keeps file order among equal sols)
        pref = [p for p in lst if p["name"] in PREFER]
        name = (pref[0] if pref else first)["name"]
        flags = 0
        for p in lst:
            flags |= MEMBERS[p["member"]]
        recs.append(dict(x=x, y=y, n=len(lst), veins=sum(1 for p in lst if p["veins"]), nod=sum(1 for p in lst if p["nodules"]),
                         flags=flags, sol0=min(p["sol"] for p in lst), sol1=max(p["sol"] for p in lst), name=name))
    recs.sort(key=lambda r: (r["sol0"], r["x"], r["y"]))
    names, offs = [], []
    o = 0
    for r in recs:
        offs.append(o); names.append(r["name"]); o += len(r["name"]) + 1
    ex = [next(i for i, r in enumerate(recs) if r["name"] == n) for n in PREFER]

    L = []
    L.append("// Mars OS map data (Ojo de Marte, from Ruta Marciana): made by extras/make_marte_data.py - do not edit by hand.")
    L.append("// Terrain: HiRISE DTM DTEEC_009149_1750_009294_1750_U01 (NASA/JPL/University of Arizona), Glen Torridon, Gale")
    L.append("// crater, sampled every ~12.1 m by Ruta Marciana. Targets: Curiosity ChemCam, Gasda et al., doi:10.17189/dw9v-xe39.")
    L.append("#pragma once")
    L.append(f"const int MR_W = {w}, MR_H = {h}, MR_N = MR_W * MR_H, MR_NSITES = {len(recs)}, MR_NTARGETS = {len(targets)};")
    L.append(f"const double MR_CELL = {t['cellMeters']!r};                 // metres per cell")
    L.append(f"const double MR_CELL_X = {t['cellXMeters']!r}; // east-west cell spacing")
    L.append(f"const int32_t MR_BASE_CM = {base};                            // the lowest cell, cm (Mars 2000 datum)")
    L.append(f"const double MR_WEST = {t['west']!r}, MR_NORTH = {t['north']!r}, MR_STEP = {t['degreeStep']!r};")
    L.append(f"const uint8_t MR_EX_A = {ex[0]}, MR_EX_B = {ex[1]};                    // the example route: Stornoway -> Whaligoe")
    L.append(f"// heights: a signed byte = cm from the guess (left + up - up-left); 0x80 = the next 2 bytes are the height itself")
    L.append(f"const uint8_t MR_TERRAIN[{len(packed)}] PROGMEM = {{")
    for i in range(0, len(packed), 32):
        L.append("  " + ",".join(str(b) for b in packed[i:i + 32]) + ",")
    L.append("};")
    L.append("// members: 1 Jura (Murray formation), 2 Knockfarril Hill, 4 Glasgow, 8 KH-GG transition (Carolyn Shoemaker formation)")
    L.append("struct MrSite { uint8_t x, y, n, veins, nodules, members; uint16_t sol0, sol1, name; };")
    L.append(f"const MrSite MR_SITES[{len(recs)}] PROGMEM = {{                    // in the order Curiosity first visited them")
    for r, off in zip(recs, offs):
        L.append(f"  {{{r['x']}, {r['y']}, {r['n']}, {r['veins']}, {r['nod']}, {r['flags']}, {r['sol0']}, {r['sol1']}, {off}}},   // {r['name']}")
    L.append("};")
    L.append("const char MR_NAMES[] PROGMEM =")
    for i in range(0, len(names), 6):
        L.append("  " + " ".join(c_str(n + "\0").replace("\0", "\\0") for n in names[i:i + 6]))
    L[-1] += ";"
    # NASA Mars facts, independently specified here; no external project input.
    facts = [dict(title="Un sol", value="24 h 39 min", description="Duracion aproximada del dia marciano."),
             dict(title="Atmosfera", value="~95% CO2", description="Atmosfera muy tenue, principalmente dioxido de carbono."),
             dict(title="Temperatura media", value="-65 C", description="Promedio aproximado; cambia con el lugar y la hora."),
             dict(title="Agua antigua", value="Rios y lagos", description="Rocas y formas del terreno conservan evidencias de agua antigua.")]
    L.append("// NASA Mars facts: https://science.nasa.gov/mars/facts/ (title, value, text in CP437)")
    L.append("const char* const MR_FACTS[%d][3] = {" % len(facts))
    for fc in facts:
        L.append("  {%s, %s,\n   %s}," % (c_bytes(cp437(fc["title"])), c_bytes(cp437(fc["value"])), c_bytes(cp437(fc["description"]))))
    L.append("};")
    L.append("const int MR_NFACTS = %d;" % len(facts))
    # Keep the supplied Adafruit font; generating map data never requires a local Arduino font.
    out = os.path.join(SKETCH, "marte_data.h")
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    from pathlib import Path
    import hashlib
    provenance = {"region":"glen-torridon", "quantization":"nearest centimetre", "maximum_error_m":max(abs(c/100-v) for c,v in zip(cm,t['elevations'])),
                  "sources":{str(Path('assets/glen-torridon')/name):hashlib.sha256(Path(data,name).read_bytes()).hexdigest() for name in ['dem.npy','routes.json','targets.json']}}
    Path(SKETCH,'map-source-sha256.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(f"{out}: terrain {len(packed)} bytes ({len(packed) - w * h} over 1 byte a cell), {len(recs)} sites, names {o} bytes,"
          f" example sites {ex}")


if __name__ == "__main__":
    main()
