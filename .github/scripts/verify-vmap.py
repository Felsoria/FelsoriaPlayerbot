import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def write_table(path, fields, record, strings=b"\0"):
    dbc = path.suffix == ".dbc"
    signature = b"WDBC" if dbc else b"WDB2"
    header = struct.pack("<4s4I", signature, 1, fields, len(record), len(strings))
    extended_header = b"" if dbc else bytes(28)
    path.write_bytes(header + extended_header + record + strings)


def chunk(id_bytes, data):
    return struct.pack("<4sI", id_bytes[::-1], len(data)) + data


def check(executable, root, *, missing_tile):
    tables = root / "unpacked" / "DBFilesClient"
    maps = root / "unpacked" / "World" / "Maps" / "Audit"
    tables.mkdir(parents=True)
    maps.mkdir(parents=True)
    (root / "wmo_list.txt").touch()
    write_table(tables / "Map.db2", 2, struct.pack("<II", 1, 0), b"Audit\0")
    write_table(tables / "LiquidType.db2", 41, bytes(256))
    write_table(tables / "GameObjectDisplayInfo.db2", 1, bytes(4))
    write_table(tables / "FileDataComplete.dbc", 1, bytes(4))

    tiles = bytearray(64 * 64 * 8)
    struct.pack_into("<I", tiles, (3 * 64 + 2) * 8, 1)
    (maps / "Audit.wdt").write_bytes(chunk(b"MAIN", tiles))
    if not missing_tile:
        (maps / "Audit_2_3_obj0.adt").write_bytes(chunk(b"MVER", struct.pack("<I", 18)))

    result = subprocess.run(
        [str(executable), "-d", "unpacked"], cwd=root, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
    )
    print(result.stdout, end="")
    if missing_tile:
        if result.returncode != 1 or "Could not convert object tile for Audit (2, 3)" not in result.stdout:
            raise RuntimeError("VMAP extraction must reject a missing declared tile.")
        if "Work complete" in result.stdout:
            raise RuntimeError("VMAP extraction reported success despite a missing tile.")
    elif result.returncode != 0 or "Work complete" not in result.stdout:
        raise RuntimeError("VMAP extraction must accept a sparse map with its declared tile.")


if __name__ == "__main__":
    extractor = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="felsoria-vmap-") as directory:
        root = Path(directory)
        check(extractor, root / "valid", missing_tile=False)
        check(extractor, root / "missing-tile", missing_tile=True)
