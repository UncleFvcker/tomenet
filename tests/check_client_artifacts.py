"""Run against a Windows client build: python tests/check_client_artifacts.py BUILD_SRC."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
makefile = re.sub(r"(?m)^#.*$", "", (build / "makefile.mingw").read_text())
objects = []
for name in ("CLI_OBJS", "CLI_LUAOBJS", "TOLUAOBJS"):
    block = re.search(rf"^{name} = (.*?)(?=\n\s*\n)", makefile, re.M | re.S)
    objects.extend(block[1].replace("\\\n", " ").split())
output = root / ".github/workspace/client-artifacts.exe"
flags = ["-std=c99", "-O2", "-g", "-D__USE_W32_SOCKETS", "-DWIN32", "-DDUMB_WIN",
         "-DMINGW", "-DMEXP=19937", "-DACC32", "-DWINVER=0x0501", "-DCLIENT_SIDE",
         "-DSOUND_SDL", "-Iclient", "-Iclient/lua", "-Iserver", "-Iserver/lua"]
for name, shim in {
    "c-store": "\nvoid test_store_key(int key) { store_process_command(key); }\n",
    "nclient": '''
void test_client_buffers(void) {
    Sockbuf_init(&wbuf, -1, CLIENT_SEND_SIZE, SOCKBUF_WRITE | SOCKBUF_READ | SOCKBUF_LOCK);
    Sockbuf_init(&rbuf, -1, CLIENT_RECV_SIZE, SOCKBUF_WRITE | SOCKBUF_READ | SOCKBUF_LOCK);
}
bool test_vault_request(bool toggle) {
    bool ok = wbuf.len && (unsigned char)wbuf.buf[0] == (toggle ? PKT_STORE_CMD : PKT_REQUEST_STORE_OPEN);
    if (toggle) ok = ok && wbuf.len == 13 && (unsigned char)wbuf.buf[1] == 0x75 && (unsigned char)wbuf.buf[2] == 0x30;
    Sockbuf_clear(&wbuf); return ok;
}
void test_catalog_packets(void) {
    Packet_printf(&rbuf, "%c%hd%s%s%hd%d%c%c%c", PKT_STORE_INFO, STORE_HOME,
        "True Artifacts 1/288", "Shared Vault", 289, 0, TERM_YELLOW, '+', 10);
    Receive_store_info(); Sockbuf_clear(&rbuf);
    Packet_printf(&rbuf, "%c%hd%c%hd%hd%d%S%c%c%d%s", PKT_STORE,
        287, TERM_SLATE, 0, 0, 0, "[Missing] Gray artifact", TV_BOOTS, 1, 0, "");
    Receive_store(); Sockbuf_clear(&rbuf);
    Packet_printf(&rbuf, "%c%hd%c%hd%hd%d%S%c%c%d%s", PKT_STORE,
        288, TERM_TRUE_ARTIFACT, 1, 1, 0, "[Stored] Gold artifact", TV_BOOTS, 1, 0, "");
    Receive_store(); Sockbuf_clear(&rbuf);
}
void test_overflow_packets(void) {
    server_version.build = 3;
    Packet_printf(&rbuf, "%c", PKT_SKILLS);
    for (int i = 0; i < 12; i++) Packet_printf(&rbuf, "%hd", i == 8 ? 20 : 0);
    assert(Receive_skills() <= 0); /* Wait for the complete raw-blows field. */
    Packet_printf(&rbuf, "%hd", 123);
    assert(Receive_skills() > 0);
    assert(p_ptr->num_blow == 20 && p_ptr->melee_blows == 123); Sockbuf_clear(&rbuf);
    server_version.build = 2;
    Packet_printf(&rbuf, "%c", PKT_SKILLS);
    for (int i = 0; i < 12; i++) Packet_printf(&rbuf, "%hd", i == 8 ? 6 : 0);
    assert(Receive_skills() > 0);
    assert(p_ptr->num_blow == 6 && p_ptr->melee_blows == 6); Sockbuf_clear(&rbuf);
    server_version.build = 3;
}
'''
}.items():
    source = output.with_name(name + "-artifact-test.c")
    obj = source.with_suffix(".o")
    source.write_text(f'#include <assert.h>\n#include "{(build / "client" / (name + ".c")).as_posix()}"\n' + shim)
    subprocess.run(["gcc", *flags, "-c", str(source), "-o", str(obj)], cwd=build, check=True)
    objects = [str(obj) if entry == f"client/{name}.o" else entry for entry in objects]
subprocess.run(["gcc", *flags, str(root / "tests/client_artifacts.c"), *objects,
                "-lkernel32", "-luser32", "-lwsock32", "-lgdi32", "-lcomdlg32", "-lwinmm",
                "-lSDL2_mixer", "-lSDL2", "-lregex", "-o", str(output)], cwd=build, check=True)
subprocess.run([str(output)], cwd=build, check=True, timeout=20)
