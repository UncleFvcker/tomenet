"""Run from the repository root with Python, MinGW gcc and MSYS make on PATH.

Links the actual server objects; wrappers isolate map/network I/O and prevent
legacy death paths from touching save files. This does not test live map changes.
Also checks the Skill potion's allocation data and actual potion effect.
Checks free respec, bonus-point persistence and legacy zero-word compatibility.
Checks unlimited world recall range with the original exploration requirement.
Checks staff recharge failures and magic ammunition damage protection.
Checks family reproduction quotas, spell cooldowns and monster save compatibility.
Checks level-zero item sharing and the remaining level/mode restrictions.
Checks physical runes on all slots and artifacts, without artifact-generation PVAL caps.
"""
from pathlib import Path
import re
import os
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
# The legacy makefile has no header dependencies; player_type changes affect all objects.
header_time = max(path.stat().st_mtime for folder in (SRC / "common", SRC / "server")
                  for path in folder.glob("*.h"))
for obj in objects:
    path = SRC / obj
    if path.exists() and path.stat().st_mtime < header_time:
        path.unlink()
subprocess.run(["make", "-f", "makefile.win", "-j4", "CFLAGS=" + " ".join(FLAGS),
                "LUACFLAGS=" + " ".join(FLAGS), *objects], cwd=SRC, check=True)
test_sources = [ROOT / "tests" / name for name in
                ("everlasting.c", "skill_potion.c", "skill_respec.c", "item_protection.c", "item_sharing.c", "physical_runes.c", "monster_rules.c")]
wrappers = re.findall(r"__wrap_(\w+)\(", "\n".join(path.read_text() for path in test_sources))
output_root = ROOT / ".github/workspace"
output_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="everlasting-", dir=output_root) as temp:
    temp = Path(temp)
    main_object = temp / "server-main.o"
    subprocess.run(["gcc", *FLAGS, "-Dmain=tomenet_server_main", "-c", "server/main.c",
                    "-o", str(main_object)], cwd=SRC, check=True)
    # Expose the existing static recall handler in the test translation unit only.
    dungeon_source = temp / "dungeon-test.c"
    dungeon_source.write_text(f'#include "{(SRC / "server/dungeon.c").as_posix()}"\n'
                              'void test_do_recall(int Ind) { do_recall(Ind, FALSE); }\n')
    dungeon_object = temp / "dungeon-test.o"
    subprocess.run(["gcc", *FLAGS, "-c", str(dungeon_source), "-o", str(dungeon_object)],
                   cwd=SRC, check=True)
    spells_source = temp / "spells-test.c"
    spells_source.write_text(f'#include "{(SRC / "server/spells1.c").as_posix()}"\n'
                            'int test_inventory_fire(void) { return inven_damage(1, set_fire_destroy, 100); }\n'
                            'void test_floor_damage(worldpos *wpos, int typ) { project_i(0, 0, 0, wpos, 0, 0, 500, typ); }\n')
    spells_object = temp / "spells-test.o"
    subprocess.run(["gcc", *FLAGS, "-c", str(spells_source), "-o", str(spells_object)],
                   cwd=SRC, check=True)
    # Test private AI and record serialization without adding production entry points.
    extra_objects = []
    for name, shim in {
        "melee2": '''
void test_cooldown_start(monster_type *m, int chance) { monster_spell_cooldown_start(m, chance); }
void test_cooldown_charge(monster_type *m, int energy) { monster_spell_cooldown_charge(m, energy); }
void test_cooldown_end(monster_type *m) { monster_spell_cooldown_end_turn(m); }
bool test_monster_moves(int m_idx) { int moves[8] = {0}; return get_moves(1, m_idx, moves); }
''',
        "monster2": "",
        "save": '''
void test_write_monsters(FILE *file, monster_type *monsters, int count, bool legacy) {
    static byte buffer[MAX_BUF_SIZE];
    fff = file; fff_buf = buffer; fff_buf_pos = 0; xor_byte = 0; v_stamp = x_stamp = 0;
    wr_u16b(count);
    for (int i = 0; i < count; i++) {
        wr_monster(&monsters[i]);
        if (legacy) { /* 4.9.25 plain records omit the six new tail bytes. */
            fff_buf_pos -= 6;
            xor_byte = fff_buf[fff_buf_pos - 1];
        }
    }
    wr_u32b(0x12345678); write_buffer(); fff = NULL; fff_buf = NULL;
}
''',
        "load2": '''
bool test_read_monsters(FILE *file, monster_type *monsters, int capacity, bool legacy) {
    static byte buffer[MAX_BUF_SIZE]; u16b count; u32b marker;
    fff = file; fff_buf = buffer; fff_buf_pos = MAX_BUF_SIZE; xor_byte = 0; v_check = x_check = 0;
    sf_major = ssf_major = 4; sf_minor = ssf_minor = 9; sf_patch = ssf_patch = legacy ? 25 : 26;
    rd_u16b(&count);
    if (count != capacity) return FALSE;
    for (int i = 0; i < count; i++) if (rd_monster(&monsters[i], TRUE)) return FALSE;
    rd_u32b(&marker); fff = NULL; fff_buf = NULL;
    return marker == 0x12345678;
}
'''
    }.items():
        source = temp / (name + "-test.c")
        body = (SRC / "server" / (name + ".c")).read_text()
        if name == "melee2":
            body = body.replace('#include "angband.h"', '#include "angband.h"\nint get_moves_astar(int Ind, int m_idx, int *yp, int *xp);')
            body = body.replace("static int get_moves_astar(", "int test_native_get_moves_astar(")
        if name == "monster2":
            # Isolate placement/ego selection I/O; quota, deletion and compaction stay real.
            body = body.replace("int place_monster_one(struct", "int test_native_place_monster_one(struct")
            body = body.replace("int pick_ego_monster(int r_idx, int Level) {",
                                "int test_native_pick_ego_monster(int r_idx, int Level) {")
        source.write_text(body + shim)
        obj = temp / (name + "-test.o")
        subprocess.run(["gcc", *FLAGS, "-c", str(source), "-o", str(obj)], cwd=SRC, check=True)
        extra_objects.append(str(obj))
    for instant_res in (True, False):
        case_flags = FLAGS.copy()
        case_objects = [obj for obj in objects if obj not in
                        ("server/main.o", "server/dungeon.o", "server/spells1.o",
                         "server/melee2.o", "server/monster2.o", "server/save.o", "server/load2.o")]
        case_objects.extend([str(dungeon_object), str(spells_object), *extra_objects])
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
        subprocess.run([str(binary)], cwd=SRC, check=True,
                       env={**os.environ, "TOMENET_TEST_DIR": str(temp)})
