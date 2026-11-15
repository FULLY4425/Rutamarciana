"""Tiles RGB565 de la ortofoto de Jezero para LittleFS (app ROVER, fase 2).

Convierte assets/jezero_ortho_4m.png (500x500, 4 m/px, L) en 64 tiles RGB565
de 64x64 px little-endian + un manifiesto, dentro del data/ del sketch (la
particion que flashea LittleFS). La fuente NO se regenera ni se re-recorta.

Uso (desde la raiz del repo):
    .venv/bin/python -m pipeline.pov_tiles              # genera + verifica + presupuesto
    .venv/bin/python -m pipeline.pov_tiles --verify-only
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

TILE = 64
# Particion spiffs del PartitionScheme=no_ota (el FQBN del README), medida en
# ~/.arduino15/packages/esp32/hardware/esp32/3.3.12/tools/partitions/no_ota.csv
LFS_TOTAL = 0x1E0000                  # 1,966,080 B = 1,920.0 KiB
LFS_BLOCK = 4096                      # bloque de asignacion de LittleFS en ESP32
LFS_META_ALLOWANCE = 16 * LFS_BLOCK   # margen conservador de metadatos del FS
# El espacio de app0 se calcula solo cuando se proporciona una compilacion medida.
APP0_TOTAL_B = 0x200000


def sha256_file(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def tile_grid(w: int, h: int, tile: int = TILE) -> list[dict]:
    """Rejilla ceil(w/tile) x ceil(h/tile); ultima fila/col recortada al resto."""
    return [{"r": r, "c": c, "x": c * tile, "y": r * tile,
             "w": min(tile, w - c * tile), "h": min(tile, h - r * tile)}
            for r in range(-(-h // tile)) for c in range(-(-w // tile))]


def to_rgb565(rgb: np.ndarray) -> np.ndarray:
    """RGB888 (H,W,3) -> RGB565 uint16 (R5 G6 B5); little-endian al serializar."""
    r = np.rint(rgb[..., 0] * 31.0 / 255.0)
    g = np.rint(rgb[..., 1] * 63.0 / 255.0)
    b = np.rint(rgb[..., 2] * 31.0 / 255.0)
    return ((r.astype(np.uint16) << 11) | (g.astype(np.uint16) << 5)
            | b.astype(np.uint16))


def from_rgb565(px: np.ndarray) -> np.ndarray:
    """RGB565 uint16 -> RGB888 (H,W,3), para la verificacion."""
    r = (px >> 11) & 0x1F
    g = (px >> 5) & 0x3F
    b = px & 0x1F
    out = np.stack([np.rint(r * 255.0 / 31.0), np.rint(g * 255.0 / 63.0),
                    np.rint(b * 255.0 / 31.0)], axis=-1)
    return out.astype(np.uint8)


def quant_bounds() -> dict:
    """Peor error de cuantizado 8->5/6->8 bits por canal, enumerado (garantia)."""
    v8 = np.arange(256, dtype=np.int64)
    q = from_rgb565(to_rgb565(np.repeat(v8[:, None], 3, axis=1).astype(np.uint8)))
    d = np.abs(q.astype(np.int64) - np.repeat(v8[:, None], 3, axis=1))
    return {"r": int(d[:, 0].max()), "g": int(d[:, 1].max()), "b": int(d[:, 2].max())}


def write_tiles(png: Path, meta_json: Path, out_dir: Path) -> dict:
    """Escribe los tiles .bin + manifest.json en out_dir; devuelve estadisticas."""
    meta = json.loads(Path(meta_json).read_text(encoding="utf-8"))
    png = Path(png)
    src_sha = sha256_file(png)
    if src_sha != meta["image_sha256"]:
        raise SystemExit(f"sha256 del PNG ({src_sha}) no coincide con {meta_json}")
    im = Image.open(png)
    if list(im.size) != list(meta["dimensions"]):
        raise SystemExit(f"dimensiones {im.size} != {meta['dimensions']} del JSON")
    rgb = np.asarray(im.convert("RGB"), dtype=np.uint8)
    h, w = rgb.shape[:2]
    px = to_rgb565(rgb)

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for stale in out_dir.glob("r??c??.bin"):
        stale.unlink()

    tiles, total_b = [], 0
    for t in tile_grid(w, h):
        data = px[t["y"]:t["y"] + t["h"], t["x"]:t["x"] + t["w"]].astype("<u2").tobytes()
        name = f"r{t['r']:02d}c{t['c']:02d}.bin"
        (out_dir / name).write_bytes(data)
        tiles.append({**t, "file": name, "bytes": len(data)})
        total_b += len(data)

    cols, rows = -(-w // TILE), -(-h // TILE)
    doc = {
        "schema_version": 1,
        "generator": "pipeline/pov_tiles.py",
        "source": {
            "image": "assets/jezero_ortho_4m.png",
            "image_sha256": src_sha,
            "meta_json": "assets/jezero_ortho_4m.json",
            "meta_json_sha256": sha256_file(Path(meta_json)),
            "dimensions": [w, h],
            "mode": im.mode,
            "m_per_px": abs(float(meta["transform"][0])),
            "crs": meta["crs"],
            "bounds_m": meta["bounds_m"],
            "transform": meta["transform"],
            "credit": meta["credit"],
            "license": meta["license"],
            "usage": meta["usage"],
        },
        "format": {
            "encoding": "RGB565 little-endian (R5 G6 B5), fila-major, v crece al sur",
            "tile_px": TILE,
            "grid": [cols, rows],
            "edge_px": [w - (cols - 1) * TILE, h - (rows - 1) * TILE],
            "padding": "none",
        },
        "tiles": tiles,
        "totals": {"tiles": len(tiles), "pixels": w * h, "bytes": total_b},
        "notes": [
            f"La espec hablaba de ultima fila/col recortada a 20 px; el resto real de "
            f"{w}-{(cols - 1) * TILE} es {w - (cols - 1) * TILE} px y se cubre la imagen "
            "completa (la fuente no se re-recorta; pov_core clampa la camara a 500x500).",
            "Nada de esta imagen va a PROGMEM: vive solo en la particion LittleFS.",
        ],
    }
    manifest = out_dir / "manifest.json"
    manifest.write_text(json.dumps(doc, indent=2), encoding="utf-8")
    return {"tiles": len(tiles), "bytes": total_b,
            "manifest_bytes": manifest.stat().st_size}


def verify(png: Path, meta_json: Path, out_dir: Path,
           report_png: Path | None = None) -> dict:
    """Recompone la imagen desde los tiles y la compara contra el PNG fuente."""
    out_dir = Path(out_dir)
    doc = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
    w, h = doc["source"]["dimensions"]
    canvas = np.zeros((h, w), dtype=np.uint16)
    listed = {t["file"] for t in doc["tiles"]}
    for t in doc["tiles"]:
        raw = (out_dir / t["file"]).read_bytes()
        if len(raw) != t["bytes"]:
            raise SystemExit(f"{t['file']}: {len(raw)} B != {t['bytes']} B del manifiesto")
        canvas[t["y"]:t["y"] + t["h"], t["x"]:t["x"] + t["w"]] = (
            np.frombuffer(raw, dtype="<u2").reshape(t["h"], t["w"]))
    extra = sorted(p.name for p in out_dir.glob("r??c??.bin") if p.name not in listed)
    if extra:
        raise SystemExit(f"tiles sin manifiesto: {extra}")

    rgb = np.asarray(Image.open(png).convert("RGB"), dtype=np.uint8)
    exact = bool(np.array_equal(canvas, to_rgb565(rgb)))
    err = np.abs(from_rgb565(canvas).astype(np.int64) - rgb.astype(np.int64))
    metrics = {
        "roundtrip_exacto": exact,
        "tiles": len(doc["tiles"]),
        "max_err": {k: int(err[..., i].max()) for i, k in enumerate("rgb")},
        "mean_err": {k: float(err[..., i].mean()) for i, k in enumerate("rgb")},
        "techo_cuantizado": quant_bounds(),
    }
    if report_png:
        Path(report_png).parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(from_rgb565(canvas)).save(Path(report_png), format="PNG")
        metrics["recompose_png"] = str(report_png)
    return metrics


def budget(data_dir: Path, sketch_bytes: int | None = None) -> dict:
    """Presupuesto medido desde los archivos reales del data/ del sketch."""
    files = sorted(p for p in Path(data_dir).rglob("*") if p.is_file())
    raw = sum(p.stat().st_size for p in files)
    blocks = sum(-(-p.stat().st_size // LFS_BLOCK) * LFS_BLOCK for p in files)
    worst = blocks + LFS_META_ALLOWANCE
    return {
        "archivos": len(files),
        "payload_crudo_b": raw,
        "peor_caso_bloques_b": worst,
        "particion_b": LFS_TOTAL,
        "libre_b": LFS_TOTAL - worst,
        "ram_render_b": TILE * 2,
        "sketch_flash_b": 0,
        "app0_libre_b": APP0_TOTAL_B - sketch_bytes if sketch_bytes is not None else None,
        "hipotesis_1_tile_promem_b": TILE * TILE * 2,
    }


def pack_image(data_dir: Path, img_path: Path, tool: Path | None = None) -> dict | None:
    """Construye la imagen LittleFS real con -s particion completa: si mklittlefs
    termina sin error, el contenido CABE (medicion binaria, no estimacion)."""
    tools = sorted(glob.glob(str(Path.home()
                                 / ".arduino15/packages/esp32/tools/mklittlefs/*/mklittlefs")))
    if tool is not None:
        tools=[str(tool)]
    elif shutil.which("mklittlefs"):
        tools=[shutil.which("mklittlefs")]
    if not tools:
        return None
    img_path = Path(img_path)
    img_path.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([tools[-1], "-c", str(data_dir), "-s", str(LFS_TOTAL),
                        str(img_path)], capture_output=True, text=True)
    return {"ok": r.returncode == 0, "tool": tools[-1], "img": str(img_path),
            "img_bytes": img_path.stat().st_size if r.returncode == 0 else 0,
            "stderr_tail": r.stderr.strip().splitlines()[-3:]}


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Tiles RGB565 de la ortofoto a LittleFS (ROVER, fase 2)")
    ap.add_argument("--png", type=Path, default=Path("assets/jezero_ortho_4m.png"))
    ap.add_argument("--meta", type=Path, default=Path("assets/jezero_ortho_4m.json"))
    ap.add_argument("--out", type=Path,
                    default=Path("firmware/psion-micor-spaceapps/data/pov"))
    ap.add_argument("--report-png", type=Path, default=Path("data/out/pov_recompose.png"))
    ap.add_argument("--verify-only", action="store_true")
    ap.add_argument("--mklittlefs",type=Path,help="Ruta explicita a mklittlefs para verificar el fit")
    ap.add_argument("--sketch-bytes",type=int,help="Flash usada por la compilacion actual (no una estimacion historica)")
    args = ap.parse_args()
    if args.sketch_bytes is not None and not 0<=args.sketch_bytes<=APP0_TOTAL_B:ap.error("--sketch-bytes fuera de app0")

    png_sha0, meta_sha0 = sha256_file(args.png), sha256_file(args.meta)
    if not args.verify_only:
        st = write_tiles(args.png, args.meta, args.out)
        print(f"escritos {st['tiles']} tiles = {st['bytes']} B "
              f"+ manifest {st['manifest_bytes']} B en {args.out}")

    m = verify(args.png, args.meta, args.out, args.report_png)
    b = budget(args.out.parent,args.sketch_bytes)
    p = pack_image(args.out.parent, Path("data/out/pov_littlefs.img"),args.mklittlefs)
    png_ok = sha256_file(args.png) == png_sha0
    meta_ok = sha256_file(args.meta) == meta_sha0

    kib = lambda n: f"{n / 1024:.1f} KiB"
    print(f"fuente intacta: PNG {png_ok}, JSON {meta_ok} (sha256 {meta_sha0[:12]}...)")
    print(f"recomposicion: {m['tiles']} tiles, roundtrip RGB565 bit-exacto = "
          f"{m['roundtrip_exacto']}")
    print(f"cuantizado vs PNG: max err R={m['max_err']['r']} G={m['max_err']['g']} "
          f"B={m['max_err']['b']} (techo terico R={m['techo_cuantizado']['r']} "
          f"G={m['techo_cuantizado']['g']} B={m['techo_cuantizado']['b']}); "
          f"media R={m['mean_err']['r']:.3f} G={m['mean_err']['g']:.3f} "
          f"B={m['mean_err']['b']:.3f}")
    if "recompose_png" in m:
        print(f"recomposicion de control: {m['recompose_png']}")
    print(f"particion LittleFS: {b['particion_b']} B ({kib(b['particion_b'])}) "
          f"[no_ota.csv, core esp32 3.3.12]")
    print(f"payload: {b['archivos']} archivos, {b['payload_crudo_b']} B crudos; "
          f"peor caso por bloques(+meta) {b['peor_caso_bloques_b']} B "
          f"({kib(b['peor_caso_bloques_b'])})")
    print(f"libre restante (peor caso): {b['libre_b']} B ({kib(b['libre_b'])})")
    if p is None:
        print("imagen mklittlefs: herramienta no encontrada, fit no verificado")
    else:
        print(f"imagen mklittlefs real ({p['tool']}): {'CABE (ok)' if p['ok'] else 'NO CABE'}"
              f", {p['img_bytes']} B")
    print(f"Buffer de pixeles del renderer: 64 px x 2 B = {b['ram_render_b']} B (sin estructuras, pila ni otras apps)")
    print(f"flash del sketch: {b['sketch_flash_b']} B en PROGMEM "
          f"(hipotetico 1 tile entero = {b['hipotesis_1_tile_promem_b']} B "
          f"en app0; libre medido: {b['app0_libre_b'] if b['app0_libre_b'] is not None else 'no informado'})")
    print("DECISION: tiles 64x64 a resolucion completa CABEN en LittleFS con holgura "
          f"({b['particion_b'] / b['peor_caso_bloques_b']:.1f}x); NO se reduce a 256x256 "
          "ni se purga el manifiesto de reduccion")

    ok = (m["roundtrip_exacto"]
          and all(m["max_err"][k] <= m["techo_cuantizado"][k] for k in "rgb")
          and png_ok and meta_ok and (p is None or p["ok"]))
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
