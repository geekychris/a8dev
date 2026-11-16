"""End-to-end tests for sessions, the control port, the debugger, the MCP
server (stdio) and DevBench (REST + MCP over HTTP). Run by ./a8 selftest.

Needs projects/hello and projects/banktest built.
"""
import asyncio
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.control import ProtocolError, SessionClient, list_sessions  # noqa: E402

HELLO = ROOT / "projects" / "hello" / "build" / "hello.xex"
BANK = ROOT / "projects" / "banktest" / "build" / "banktest.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def section(t):
    print(f"\n== {t}")


def test_session():
    section("session process + control port")
    c = SessionClient.spawn(str(HELLO))
    try:
        pid = c.info["pid"]
        check("session registered", any(s["pid"] == pid for s in list_sessions()))
        st = c.call("status")
        check("status: 130xe, paused, symbols", st["machine"] == "130xe" and st["paused"] and st["symbols"] > 100, str(st))
        r = c.call("run_until", text="hello: ready", max_frames=500)
        check("run_until", r["found"], str(r))
        f0 = c.call("status")["frame"]
        c.call("run", frames=7)
        check("run advances exactly N frames", c.call("status")["frame"] == f0 + 7)
        c.call("log")
        c.call("press", keys="RIGHT", hold_frames=3)
        r = c.call("run_until", text="hello: move x=", max_frames=20)
        check("press (joystick) + log", r["found"], str(r))
        c.call("hold", keys="FIRE")
        r = c.call("run_until", text="hello: fire", max_frames=20)
        c.call("release")
        check("hold / release", r["found"])
        c.call("press", keys="START")
        check("console key START", c.call("run_until", text="hello: start", max_frames=20)["found"])
        c.call("press", keys="SPACE")
        check("keyboard SPACE = fire", c.call("run_until", text="hello: fire", max_frames=20)["found"])
        r = c.call("screenshot", scale=1)
        check("screenshot", r["width"] >= 320 and r["height"] >= 192 and r["colors"] >= 3, str(r)[:200])

        section("debugger")
        r = c.call("symbols", pattern="^_main$")
        check("symbols", r and r[0]["name"] == "_main", str(r))
        r = c.call("disasm", addr="main", count=2)
        check("disasm by symbol", "_main" in r["listing"].splitlines()[0], r["listing"])
        r = c.call("read_mem", addr="bx", length=1)
        check("read_mem by symbol", len(r["hex"]) == 2, str(r))
        c.call("write_mem", addr="sx", hex="50")
        check("write_mem", c.call("read_mem", addr="sx", length=1)["hex"] == "50")
        c.call("watch", addr="bx", length=1, kind="w")
        r = c.call("continue", max_frames=5)
        check("watchpoint stops on the write", r["stopped"] and "main" in (r.get("symbol") or ""), str(r)[:300])
        c.call("delete")
        r = c.call("continue", max_frames=5)
        check("delete all -> runs freely", not r["stopped"] and r["frames"] == 5, str(r)[:200])
        c.call("break", addr="frame_wait")
        r = c.call("continue", max_frames=3)
        check("breakpoint on a function", r["stopped"] and "frame_wait" in (r.get("symbol") or ""), str(r)[:300])
        r = c.call("step_over")
        r = c.call("stepi", count=3)
        check("stepi", "PC" in r["regs"] and r.get("next"), str(r)[:200])
        check("backtrace", isinstance(c.call("backtrace", items=4), (list, dict)))
        c.call("delete")
        regs = c.call("set_reg", reg="A", value=0x12)
        check("set_reg", regs["A"] == 0x12, str(regs))

        section("snapshots + machines")
        p = c.call("save_state", path="selftest")["path"]
        v = c.call("read_mem", addr="bx", length=1)["hex"]
        c.call("run", frames=20)
        c.call("load_state", path="selftest")
        check("save_state / load_state round trip", c.call("read_mem", addr="bx", length=1)["hex"] == v, p)
        r = c.call("load", path=str(BANK))
        check("banktest passes in the session", c.call("run_until", text="A8:DONE", max_frames=3000)["found"])
        r = c.call("read_mem", addr=0, length=4, bank=1)
        check("read_mem bank=1", r["hex"] == bytes((i * 7 + 31 + (i >> 8)) & 0xFF for i in range(4)).hex(), str(r))
        check("memory_map", isinstance(c.call("memory_map"), dict))
        try:
            c.call("nope")
            check("unknown command is an error", False)
        except ProtocolError:
            check("unknown command is an error", True)
    finally:
        c.close(stop=True)
    time.sleep(0.5)
    check("session unregistered on quit", not any(s["pid"] == pid for s in list_sessions()))


def test_cli_ctl():
    section("./a8 ctl against a served session")
    c = SessionClient.spawn(str(HELLO))
    try:
        port = str(c.info["control_port"])
        a8 = [str(ROOT / ".venv" / "bin" / "a8dev"), "ctl", "--port", port]
        r = subprocess.run(a8 + ["run_until", "text=hello: ready"], capture_output=True, text=True)
        check("ctl run_until", r.returncode == 0 and '"found": true' in r.stdout, r.stdout + r.stderr)
        r = subprocess.run(a8 + ["disasm", "addr=main", "count=2"], capture_output=True, text=True)
        check("ctl prints listings plainly", "_main" in r.stdout.splitlines()[0] if r.stdout else False, r.stdout)
    finally:
        c.close(stop=True)


def test_mcp():
    section("MCP server (stdio)")
    from mcp.client.session import ClientSession
    from mcp.client.stdio import StdioServerParameters, stdio_client

    async def run():
        p = StdioServerParameters(command=str(ROOT / ".venv" / "bin" / "a8dev-mcp"), args=[])
        async with stdio_client(p) as (r, w):
            async with ClientSession(r, w) as s:
                await s.initialize()
                names = {t.name for t in (await s.list_tools()).tools}
                check("MCP exposes build/run/debug tools",
                      {"build", "emu_boot", "emu_run_until", "emu_look", "emu_press", "emu_read_mem",
                       "emu_break", "emu_watch", "emu_continue", "emu_regs", "emu_cmd", "emu_joystick",
                       "emu_record_input", "emu_replay", "emu_screen_hash", "emu_record_video"} <= names,
                      str(sorted(names)))

                async def call(n, **a):
                    return await s.call_tool(n, a)

                res = await call("build", project="hello")
                check("MCP build", "build hello: OK" in res.content[0].text, res.content[0].text[-300:])
                await call("emu_boot", project="hello")
                res = await call("emu_run_until", text="hello: ready", max_frames=500)
                check("MCP emu_run_until", '"found": true' in res.content[0].text, res.content[0].text[:200])
                res = await call("emu_look", scale=1)
                check("MCP emu_look returns an image", [c.type for c in res.content] == ["image"])
                await call("emu_press", keys="FIRE")
                res = await call("emu_run_until", text="hello: fire", max_frames=20)
                check("MCP emu_press", '"found": true' in res.content[0].text)
                await call("emu_press", keys="START")
                res = await call("emu_run_until", text="hello: start", max_frames=20)
                check("MCP console key", '"found": true' in res.content[0].text)
                res = await call("emu_break", addr="snd_note")
                res = await call("emu_press", keys="FIRE")      # fire plays a note -> breaks
                check("MCP breakpoint hit during press", '"reason": "break"' in res.content[0].text
                      and "snd_note" in res.content[0].text, res.content[0].text[:300])
                await call("emu_delete")
                res = await call("emu_read_mem", addr="bx", length=2)
                check("MCP emu_read_mem", len(res.content[0].text.split()) >= 3, res.content[0].text)
                res = await call("emu_screen_hash")
                check("MCP emu_screen_hash", len(res.content[0].text.strip()) >= 8)
                await call("emu_record_input", action="start")
                await call("emu_press", keys="LEFT")
                await call("emu_run", frames=20)
                rp = str(ROOT / "build" / "mcp-rec.json")
                res = await call("emu_record_input", action="save", path=rp)
                check("MCP emu_record_input start/save", '"hashes"' in res.content[0].text, res.content[0].text[:300])
                res = await call("emu_replay", path=rp)
                check("MCP emu_replay (per-frame hashes)", '"ok": true' in res.content[0].text
                      and '"mismatches": 0' in res.content[0].text, res.content[0].text[:300])
                res = await call("emu_record_video", path=str(ROOT / "build" / "mcp.gif"), frames=10)
                check("MCP emu_record_video", '"frames": 10' in res.content[0].text, res.content[0].text[:200])
                res = await call("emu_load", path="banktest")
                await call("emu_run_until", text="A8:DONE", max_frames=3000)
                res = await call("emu_read_mem", addr="0", length=4, bank=3)
                check("MCP banked read (130XE)", res.content[0].text.startswith("0000"), res.content[0].text)
                res = await call("emu_hw")
                check("MCP emu_hw", '"pia"' in res.content[0].text, res.content[0].text[:200])
                res = await call("emu_read_mem", addr="$E000", length=4, space="ram")
                check("MCP read RAM under the OS ROM", res.content[0].text.startswith("E000"), res.content[0].text)
                await call("emu_load", path="hello")
                res = await call("emu_run_until_signal", max_frames=3000)
                check("MCP emu_run_until_signal (a8_done -> 0)", '"signal": 0' in res.content[0].text
                      or '"signal": "0"' in res.content[0].text, res.content[0].text[:400])
                res = await call("emu_run_to_addr", addr="frame_wait", max_frames=5)
                check("MCP emu_run_to_addr", "frame_wait" in res.content[0].text, res.content[0].text[:300])
                await call("emu_console", keys="START")
                await call("emu_run", frames=3)
                await call("emu_console", keys="")
                res = await call("emu_log")
                check("MCP emu_console holds START", "hello: start" in res.content[0].text, res.content[0].text[-300:])
                res = await call("emu_position")
                check("MCP emu_position", '"since_load"' in res.content[0].text)
                await call("emu_close")

    asyncio.run(asyncio.wait_for(run(), 300))


def test_devbench():
    section("DevBench REST + MCP over HTTP")
    sk = socket.socket()
    sk.bind(("127.0.0.1", 0))
    port = sk.getsockname()[1]
    sk.close()
    base = f"http://127.0.0.1:{port}"
    srv = subprocess.Popen([str(ROOT / ".venv" / "bin" / "a8dev"), "devbench", "--port", str(port)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def req(method, path, body=None, raw=False):
        r = urllib.request.Request(base + path, method=method,
                                   data=json.dumps(body).encode() if body is not None else None,
                                   headers={"content-type": "application/json"} if body is not None else {})
        try:
            with urllib.request.urlopen(r, timeout=120) as resp:
                data = resp.read()
                return resp.status, (data if raw else json.loads(data))
        except urllib.error.HTTPError as ex:
            return ex.code, json.loads(ex.read() or b"{}")

    pid = None
    try:
        for _ in range(100):
            try:
                if req("GET", "/api/health")[1]["ok"]:
                    break
            except OSError:
                time.sleep(0.2)
        check("devbench serves /api/health", req("GET", "/api/health")[0] == 200)
        st, ui = req("GET", "/", raw=True)
        check("devbench serves the web UI", st == 200 and b"A8 DevBench" in ui)
        st, ps = req("GET", "/api/projects")
        check("projects listed", st == 200 and {"hello", "banktest"} <= {p["name"] for p in ps})
        st, r = req("POST", "/api/sessions", {"project": "hello"})
        pid = r.get("pid")
        check("POST /api/sessions starts a session", st == 200 and pid, str(r))
        st, r = req("POST", f"/api/sessions/{pid}/cmd/run_until", {"text": "hello: ready"})
        check("REST run_until", st == 200 and r["found"], str(r))
        st, png = req("GET", f"/api/sessions/{pid}/screen.png", raw=True)
        check("screen.png is a PNG", st == 200 and png[:4] == b"\x89PNG")
        st, png = req("GET", f"/api/sessions/{pid}/screen.png?t=123", raw=True)
        check("screen.png ignores the pages' t= cache-buster", st == 200 and png[:4] == b"\x89PNG")
        st, r = req("GET", "/api/cmd/regs")
        check("GET on the selected session", st == 200 and "PC" in r)
        st, page = req("GET", "/arcade", raw=True)
        check("the arcade page", st == 200 and b"A8 Arcade" in page)
        check("the DevBench links to the arcade", b'href="/arcade"' in ui)
        st, a = req("GET", "/api/arcade")
        dirs = {d.name for d in (ROOT / "projects").iterdir() if (d / "Makefile").exists()}
        listed = {g["project"] for g in a["games"]} | {g["project"] for g in a["others"]}
        check("GET /api/arcade lists every project (games + others)", st == 200 and listed == dirs,
              str(dirs ^ listed))
        check("every game in projects/ with an arcade.json is a game there",
              {g["project"] for g in a["games"]} == {d for d in dirs if (ROOT / "projects" / d / "arcade.json").exists()})
        check("no arcade.json problems", not [g for g in a["games"] if g.get("problem")],
              str([(g["project"], g["problem"]) for g in a["games"] if g.get("problem")]))
        check("every game has a picture", all(g["has_picture"] for g in a["games"]),
              str([g["project"] for g in a["games"] if not g["has_picture"]]))
        st, png = req("GET", "/api/arcade/picture/nova_defense.png", raw=True)
        check("a game's picture", st == 200 and png[:4] == b"\x89PNG")
        st, r = req("POST", "/api/arcade/play", {"game": "hello", "window": False, "direct": True})
        check("POST /api/arcade/play direct: the game on its own, to its ready line",
              st == 200 and r.get("via") == "direct" and r.get("started"), str(r)[:300])
        if st == 200:
            req("DELETE", f"/api/sessions/{r['session']['pid']}")
        if a.get("built"):
            st, r = req("POST", "/api/arcade/play", {"game": "nova_defense", "window": False})
            check("POST /api/arcade/play boots the arcade disk and starts the game from the menu",
                  st == 200 and r.get("via") == "menu" and r.get("started"), str(r)[:300])
            if st == 200:
                req("DELETE", f"/api/sessions/{r['session']['pid']}")
        check("unknown command -> 400", req("GET", "/api/cmd/nope")[0] == 400)
        check("bad session -> 404", req("GET", "/api/sessions/1/cmd/status")[0] == 404)
        init = {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}}}
        r = urllib.request.Request(base + "/mcp", data=json.dumps(init).encode(), method="POST", headers={
            "content-type": "application/json", "accept": "application/json, text/event-stream"})
        with urllib.request.urlopen(r, timeout=20) as resp:
            check("MCP over HTTP at /mcp", b'"serverInfo"' in resp.read())
    finally:
        if pid:
            req("DELETE", f"/api/sessions/{pid}")
        srv.terminate()
        srv.wait(10)


def main():
    for f in (HELLO, BANK):
        if not f.exists():
            print(f"build first: {f} missing (./a8 build hello / banktest)")
            return 2
    test_session()
    test_cli_ctl()
    test_mcp()
    test_devbench()
    print(f"\n{'FAIL' if failures else 'PASS'}: {len(failures)} failure(s)" if failures else "\nPASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
