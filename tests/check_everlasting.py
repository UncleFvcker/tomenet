"""Run from the repository root with Python, MinGW gcc and MSYS make on PATH.

Links the actual server objects; wrappers isolate map/network I/O and prevent
legacy death paths from touching save files. This does not test live map changes.
Also checks the Skill potion's allocation data and actual potion effect.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
FLAGS = ["-std=c99", "-O2", "-g", "-Wall", "-D__USE_W32_SOCKETS", "-DWIN32",
         "-DDUMB_WIN", "-DMINGW", "-DMEXP=19937", "-DACC32", "-DWINVER=0x0501",
         "-Iserver", "-Iserver/lua"]
kind_data = (ROOT / "lib/game/k_info.txt").read_text()
kinds = re.split(r"(?m)^N:", kind_data)[1:]
skill = next(kind for kind in kinds if kind.startswith("1076:Skill\n"))
for stat in (48, 49, 50, 51, 52, 53):
    potion = next(kind for kind in kinds if f"\nI:71:{stat}:0\n" in kind)
    for tag in ("W", "A", "P", "F"):
        assert re.search(rf"(?m)^{tag}:(.*)$", skill)[1] == re.search(rf"(?m)^{tag}:(.*)$", potion)[1]
assert "\nI:72:22:0\n" in skill
assert sum("\nI:72:22:" in kind for kind in kinds) == 1
makefile = (SRC / "makefile.mingw").read_text()
objects = []
for name in ("SERV_OBJS", "LUAOBJS", "TOLUAOBJS"):
    block = re.search(rf"^{name} = (.*?)(?=\n\s*\n)", makefile, re.M | re.S)
    objects.extend(block[1].replace("\\\n", " ").split())
objects = list(dict.fromkeys(objects))
subprocess.run(["make", "-f", "makefile.win", "-j4", "CFLAGS=" + " ".join(FLAGS),
                "LUACFLAGS=" + " ".join(FLAGS), *objects], cwd=SRC, check=True)
test_sources = [ROOT / "tests/everlasting.c", ROOT / "tests/skill_potion.c"]
wrappers = re.findall(r"__wrap_(\w+)\(", "\n".join(path.read_text() for path in test_sources))
output_root = ROOT / ".github/workspace"
output_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="everlasting-", dir=output_root) as temp:
    temp = Path(temp)
    main_object = temp / "server-main.o"
    subprocess.run(["gcc", *FLAGS, "-Dmain=tomenet_server_main", "-c", "server/main.c",
                    "-o", str(main_object)], cwd=SRC, check=True)
    for instant_res in (True, False):
        case_flags = FLAGS.copy()
        case_objects = [obj for obj in objects if obj != "server/main.o"]
        if not instant_res:
            header = temp / "without-instant-res.h"
            header.write_text('#define SERVER\n#include "angband.h"\n#undef ENABLE_INSTANT_RES\n')
            case_flags += ["-include", str(header)]
            death_object = temp / "death.o"
            subprocess.run(["gcc", *case_flags, "-c", "server/xtra2.c", "-o", str(death_object)],
                           cwd=SRC, check=True)
            case_objects = [obj for obj in case_objects if obj != "server/xtra2.o"] + [str(death_object)]
        binary = temp / "everlasting.exe"
        subprocess.run(["gcc", *case_flags, *map(str, test_sources),
                        *case_objects, str(main_object),
                        *["-Wl,--wrap=" + name for name in sorted(set(wrappers))],
                        "-lkernel32", "-luser32", "-lwsock32", "-lgdi32", "-lcomdlg32",
                        "-lwinmm", "-lregex", "-o", str(binary)], cwd=SRC, check=True)
        subprocess.run([str(binary)], cwd=SRC, check=True)
