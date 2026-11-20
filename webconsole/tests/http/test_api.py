#!/usr/bin/env python3
"""End-to-end tests for the DISKMODE Web Console HTTP API.

Starts the real server binary against a temporary, simulated storage tree
(fake volume rules pointing at temp directories) and exercises
authentication, CSRF, authorization, path traversal, file operations,
uploads, downloads, jobs, inbox, search, screen and the format simulator.
No real device is touched. Usage: test_api.py <server-binary> <web-root>
"""
import http.client, json, os, shutil, socket, subprocess, sys, tempfile, time, hashlib, struct, unittest, urllib.parse

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/diskmode-webconsole"
WEB = sys.argv[2] if len(sys.argv) > 2 else "web"
PAIR = "TESTCODE1"
ADMIN_PW = "adminpw"


def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def make_wav(frames=1000, channels=1, bits=16):
    block = channels * bits // 8
    data = bytearray()
    for f in range(frames):
        v = 0.5 if (f % 100) < 50 else -0.5
        for _ in range(channels):
            data += struct.pack("<h", int(v * 32767))
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE" + b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, 44100, 44100 * block, block, bits) + b"data" + struct.pack("<I", len(data))
    return bytes(hdr + data)


class Client:
    def __init__(self, port):
        self.port = port; self.cookie = None; self.csrf = None

    def req(self, method, path, body=None, headers=None, raw=False, decode=True):
        h = {"Host": "127.0.0.1:%d" % self.port}
        if self.cookie: h["Cookie"] = self.cookie
        if self.csrf and method in ("POST", "PUT", "DELETE"): h["X-DM-CSRF"] = self.csrf
        if headers: h.update(headers)
        data = None
        if body is not None:
            if raw: data = body
            else: data = json.dumps(body).encode(); h["Content-Type"] = "application/json"
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
        c.request(method, path, body=data, headers=h)
        r = c.getresponse(); content = r.read(); hdrs = {k.lower(): v for k, v in r.getheaders()}
        set_cookie = [v for k, v in r.getheaders() if k.lower() == "set-cookie"]
        for sc in set_cookie:
            if sc.startswith("dm_session="):
                val = sc.split(";")[0]
                self.cookie = None if val == "dm_session=" else val
        c.close()
        if decode and hdrs.get("content-type", "").startswith("application/json"):
            try: content = json.loads(content.decode())
            except Exception: pass
        return r.status, content, hdrs

    def pair(self, role="file_manager", code=PAIR):
        st, j, _ = self.req("POST", "/api/v1/auth/pair", {"code": code, "role": role})
        if st == 200: self.csrf = j["csrf"]
        return st, j

    def wait_job(self, jid, timeout=20):
        t0 = time.time()
        while time.time() - t0 < timeout:
            st, j, _ = self.req("GET", "/api/v1/jobs/" + jid)
            if j["state"] not in ("queued", "running"): return j
            time.sleep(0.05)
        raise AssertionError("job did not finish: " + jid)


class ServerFixture:
    def __init__(self):
        self.root = tempfile.mkdtemp(prefix="dm-http-")
        self.sd = os.path.join(self.root, "media", "3265-3932"); self.data = os.path.join(self.root, "data", "UserContent", "Samples")
        for d in ("Samples/User", "Samples/Kit", "Projects", "autorun"): os.makedirs(os.path.join(self.sd, d))
        os.makedirs(self.data)
        with open(os.path.join(self.sd, "Samples/User/kick.wav"), "wb") as f: f.write(make_wav())
        with open(os.path.join(self.sd, "Samples/User/snare.wav"), "wb") as f: f.write(make_wav(600))
        with open(os.path.join(self.sd, "Samples/Kit/hat.wav"), "wb") as f: f.write(make_wav(300))
        with open(os.path.join(self.sd, "autorun/autorun.sh"), "w") as f: f.write("#!/bin/sh\n")
        with open(os.path.join(self.data, "factory.wav"), "wb") as f: f.write(make_wav(200))
        os.symlink("/etc", os.path.join(self.sd, "Samples", "evil-link"))
        self.appdata = os.path.join(self.root, "appdata"); os.makedirs(self.appdata)
        rules = [
            {"id": "sdcard", "name": "microSD card", "kind": "removable", "mount_point": self.sd, "directory": True, "writable": True, "format_candidate": True, "protected_paths": ["autorun", "mpcqol.conf", ".diskmode"]},
            {"id": "internal-samples", "name": "Internal: Samples", "kind": "internal", "mount_point": os.path.join(self.root, "data"), "directory": True, "subdir": "UserContent/Samples", "writable": False},
            {"id": "ghost", "name": "Unplugged USB", "kind": "usb", "mount_point": "/nonexistent/mount", "directory": True},
        ]
        self.rules = os.path.join(self.root, "volumes.json")
        with open(self.rules, "w") as f: json.dump(rules, f)
        admin_hash = subprocess.check_output([BIN, "--hash-password", ADMIN_PW]).decode().strip()
        cfg = {"bind_host": "127.0.0.1", "port": 0, "data_dir": self.appdata, "web_root": os.path.abspath(WEB), "allow_plain_http": False,
               "auth": {"pairing_code": PAIR, "admin_password_hash": admin_hash, "max_failures": 4, "lockout_seconds": 2},
               "upload_chunk_max": 65536 * 4, "screen_max_fps": 30, "log_level": "warn", "application_medium_mount": ""}
        self.cfgpath = os.path.join(self.root, "config.json")
        with open(self.cfgpath, "w") as f: json.dump(cfg, f)
        self.port = free_port()
        self.proc = subprocess.Popen([BIN, "--config", self.cfgpath, "--volumes-json", self.rules, "--port", str(self.port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for _ in range(100):
            try:
                c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=1); c.request("GET", "/api/v1/health", headers={"Host": "127.0.0.1"}); c.getresponse().read(); c.close(); break
            except Exception: time.sleep(0.05)
        else:
            raise RuntimeError("server did not start: " + self.proc.stderr.read().decode())

    def mounts_file_table(self):
        return None

    def stop(self):
        self.proc.terminate()
        try: self.proc.wait(5)
        except subprocess.TimeoutExpired: self.proc.kill()
        shutil.rmtree(self.root, ignore_errors=True)


FX = None


class ApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        global FX
        FX = ServerFixture()

    @classmethod
    def tearDownClass(cls):
        FX.stop()

    def c(self): return Client(FX.port)

    # ---------------- security
    def test_unauthenticated_is_refused(self):
        c = self.c()
        for path in ("/api/v1/volumes", "/api/v1/files?volume=sdcard&path=", "/api/v1/storage/status", "/api/v1/screen/frame", "/api/v1/admin/status"):
            st, j, _ = c.req("GET", path); self.assertEqual(st, 401, path)
        st, _, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "x"}); self.assertEqual(st, 401)
        st, j, _ = c.req("GET", "/api/v1/auth/status"); self.assertEqual(st, 200); self.assertFalse(j["authenticated"])
        self.assertFalse(os.path.exists(os.path.join(FX.sd, "x")))

    def test_pairing_rate_limit_and_roles(self):
        c = self.c()
        st, j = c.pair(code="nope"); self.assertEqual(st, 403)
        st, j = c.pair(role="admin"); self.assertEqual(st, 400)   # admin cannot be requested by pairing
        st, j = c.pair(); self.assertEqual(st, 200); self.assertEqual(j["role"], "file_manager"); self.assertIn("dm_session=", c.cookie)
        self.assertNotIn("csrf", c.cookie)
        st, j, hd = c.req("GET", "/api/v1/auth/status"); self.assertTrue(j["authenticated"]); self.assertEqual(j["csrf"], c.csrf)
        # lockout after repeated failures
        bad = self.c()
        for _ in range(4): bad.pair(code="wrong")
        st, j = bad.pair(); self.assertEqual(st, 409); self.assertEqual(j["error"], "busy")
        time.sleep(2.2)
        st, j = bad.pair(); self.assertEqual(st, 200)

    def test_csrf_protection(self):
        c = self.c(); c.pair()
        saved = c.csrf; c.csrf = None
        st, j, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "csrf-dir"}); self.assertEqual(st, 403); self.assertEqual(j["error"], "csrf")
        c.csrf = "0" * 64
        st, j, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "csrf-dir"}); self.assertEqual(st, 403)
        c.csrf = saved
        st, j, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "csrf-dir"}, headers={"Sec-Fetch-Site": "cross-site"}); self.assertEqual(st, 403)
        st, j, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "csrf-dir"}, headers={"Origin": "https://evil.example"}); self.assertEqual(st, 403)
        self.assertFalse(os.path.exists(os.path.join(FX.sd, "csrf-dir")))
        st, j, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "csrf-dir"}, headers={"Origin": "http://127.0.0.1:%d" % FX.port, "Sec-Fetch-Site": "same-origin"}); self.assertEqual(st, 200)
        self.assertTrue(os.path.isdir(os.path.join(FX.sd, "csrf-dir")))

    def test_host_header_and_security_headers(self):
        c = self.c()
        st, j, hd = c.req("GET", "/api/v1/health", headers={"Host": "attacker.example.com"}); self.assertEqual(st, 421)
        st, j, hd = c.req("GET", "/api/v1/health", headers={"Host": "mpc-sample.local"}); self.assertEqual(st, 200)
        st, j, hd = c.req("GET", "/api/v1/health")
        self.assertEqual(hd["x-content-type-options"], "nosniff"); self.assertIn("frame-ancestors 'none'", hd["content-security-policy"]); self.assertEqual(hd["cache-control"], "no-store")

    def test_permission_escalation_blocked(self):
        v = self.c(); v.pair(role="viewer")
        st, j, _ = v.req("GET", "/api/v1/files?volume=sdcard&path=Samples"); self.assertEqual(st, 200)
        st, j, _ = v.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "viewer-dir"}); self.assertEqual(st, 403); self.assertIn("file_manager", j["message"])
        st, j, _ = v.req("POST", "/api/v1/files/delete", {"items": [{"volume": "sdcard", "path": "Samples/User/kick.wav"}]}); self.assertEqual(st, 403)
        st, j, _ = v.req("POST", "/api/v1/admin/format/preflight", {"volume": "sdcard"}); self.assertEqual(st, 403)
        st, j, _ = v.req("GET", "/api/v1/session/list"); self.assertEqual(st, 403)
        fm = self.c(); fm.pair()
        st, j, _ = fm.req("POST", "/api/v1/admin/format/preflight", {"volume": "sdcard"}); self.assertEqual(st, 403)
        st, j, _ = fm.req("POST", "/api/v1/auth/elevate", {"password": "wrong"}); self.assertEqual(st, 403)
        st, j, _ = fm.req("POST", "/api/v1/auth/elevate", {"password": ADMIN_PW}); self.assertEqual(st, 200); self.assertEqual(j["role"], "admin")
        st, j, _ = fm.req("GET", "/api/v1/session/list"); self.assertEqual(st, 200); self.assertTrue(any(s["current"] for s in j["sessions"]))
        self.assertTrue(os.path.exists(os.path.join(FX.sd, "Samples/User/kick.wav")))

    def test_session_revocation_and_logout(self):
        a = self.c(); a.pair(); a.req("POST", "/api/v1/auth/elevate", {"password": ADMIN_PW})
        b = self.c(); b.pair(role="viewer")
        st, j, _ = a.req("GET", "/api/v1/session/list")
        handles = [s["handle"] for s in j["sessions"] if not s["current"] and not s["revoked"] and s["role"] == "viewer"]
        self.assertTrue(handles)
        for h in handles: a.req("POST", "/api/v1/session/revoke", {"handle": h})
        st, j, _ = b.req("GET", "/api/v1/volumes"); self.assertEqual(st, 401); self.assertIn("revoked", j["message"])
        st, j, _ = a.req("POST", "/api/v1/auth/logout"); self.assertEqual(st, 200)
        st, j, _ = a.req("GET", "/api/v1/volumes"); self.assertEqual(st, 401)

    def test_path_traversal_and_symlink_escape(self):
        c = self.c(); c.pair()
        for p in ("../", "..%2F..%2Fetc", "Samples/../../etc", "/etc/passwd", "Samples/evil-link", "Samples/evil-link/passwd", "a//b", "a\\b"):
            st, j, _ = c.req("GET", "/api/v1/files?volume=sdcard&path=" + urllib.parse.quote(p, safe=""))
            self.assertIn(st, (400, 403, 404), p); self.assertFalse(j["ok"], p)
        st, j, _ = c.req("GET", "/api/v1/files/download?volume=sdcard&path=Samples%2Fevil-link%2Fpasswd"); self.assertIn(st, (403, 404))
        st, j, _ = c.req("GET", "/api/v1/files?volume=../sdcard&path="); self.assertEqual(st, 400)
        st, j, _ = c.req("GET", "/api/v1/files?volume=nope&path="); self.assertEqual(st, 404)
        st, j, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "autorun/hack"}); self.assertEqual(st, 403); self.assertEqual(j["error"], "forbidden")
        st, j, _ = c.req("POST", "/api/v1/files/rename", {"volume": "sdcard", "path": "autorun", "name": "autorun-off"}); self.assertEqual(st, 403)
        st, j, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "internal-samples", "path": "newdir"}); self.assertEqual(st, 403); self.assertEqual(j["error"], "read_only")
        # listing hides the symlink entirely
        st, j, _ = c.req("GET", "/api/v1/files?volume=sdcard&path=Samples"); self.assertEqual(st, 200)
        self.assertNotIn("evil-link", [e["name"] for e in j["entries"]])

    # ---------------- volumes and files
    def test_volumes_and_listing(self):
        c = self.c(); c.pair()
        st, j, _ = c.req("GET", "/api/v1/volumes"); self.assertEqual(st, 200)
        vols = {v["id"]: v for v in j["volumes"]}
        self.assertTrue(vols["sdcard"]["writable"]); self.assertTrue(vols["sdcard"]["mounted"]); self.assertEqual(vols["sdcard"]["ownership"], "device")
        self.assertFalse(vols["internal-samples"]["writable"]); self.assertTrue(vols["internal-samples"]["mounted"]); self.assertIn("list", vols["internal-samples"]["allowed_ops"]); self.assertNotIn("mkdir", vols["internal-samples"]["allowed_ops"])
        self.assertFalse(vols["ghost"]["mounted"]); self.assertEqual(vols["ghost"]["ownership"], "unmounted")
        st, j, _ = c.req("GET", "/api/v1/files?volume=ghost&path="); self.assertEqual(st, 503)
        st, j, _ = c.req("GET", "/api/v1/files?volume=sdcard&path=&sort=name"); self.assertEqual(st, 200)
        names = [e["name"] for e in j["entries"]]; self.assertIn("Samples", names); self.assertIn("autorun", names)
        self.assertTrue([e for e in j["entries"] if e["name"] == "autorun"][0]["protected"])
        st, full, _ = c.req("GET", "/api/v1/files?volume=sdcard&path=Samples/User")
        largest = max(full["entries"], key=lambda e: e["size"])["name"]
        st, j, _ = c.req("GET", "/api/v1/files?volume=sdcard&path=Samples/User&sort=size&desc=1&limit=1"); self.assertEqual(len(j["entries"]), 1); self.assertEqual(j["total"], full["total"]); self.assertGreaterEqual(j["total"], 2); self.assertEqual(j["entries"][0]["name"], largest)
        st, j, _ = c.req("GET", "/api/v1/files?volume=sdcard&path=Samples/User&kind=audio&filter=sna"); self.assertEqual([e["name"] for e in j["entries"]], ["snare.wav"])
        st, j, _ = c.req("GET", "/api/v1/files/stat?volume=sdcard&path=Samples/User/kick.wav"); self.assertEqual(st, 200); self.assertEqual(j["kind"], "audio"); self.assertTrue(j["wav"]["supported"])
        st, j, _ = c.req("GET", "/api/v1/files/peaks?volume=sdcard&path=Samples/User/kick.wav&buckets=32"); self.assertEqual(st, 200); self.assertEqual(len(j["peaks"]), 64)
        st, j, _ = c.req("GET", "/api/v1/files/hash?volume=sdcard&path=Samples/User/kick.wav"); self.assertEqual(j["sha256"], hashlib.sha256(make_wav()).hexdigest())

    def test_download_with_range(self):
        c = self.c(); c.pair()
        st, body, hd = c.req("GET", "/api/v1/files/download?volume=sdcard&path=Samples/User/kick.wav", decode=False)
        self.assertEqual(st, 200); self.assertEqual(body, make_wav()); self.assertIn("attachment", hd["content-disposition"]); self.assertEqual(hd["content-type"], "audio/wav")
        st, body, hd = c.req("GET", "/api/v1/files/download?volume=sdcard&path=Samples/User/kick.wav&inline=1", headers={"Range": "bytes=0-3"}, decode=False)
        self.assertEqual(st, 206); self.assertEqual(body, b"RIFF")
        st, body, hd = c.req("GET", "/api/v1/files/download?volume=internal-samples&path=factory.wav", decode=False); self.assertEqual(st, 200)
        st, j, hd = c.req("GET", "/api/v1/files/download?volume=sdcard&path=Samples"); self.assertEqual(st, 400)
        st, j, _ = c.req("GET", "/api/v1/recents"); self.assertTrue(any(r["path"] == "Samples/User/kick.wav" for r in j["recents"]))

    def test_copy_move_delete_restore_jobs(self):
        c = self.c(); c.pair()
        st, j, _ = c.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "Samples/Work"}); self.assertEqual(st, 200)
        st, j, _ = c.req("POST", "/api/v1/files/copy", {"items": [{"volume": "sdcard", "path": "Samples/User/kick.wav"}, {"volume": "sdcard", "path": "Samples/Kit"}], "dest": {"volume": "sdcard", "path": "Samples/Work"}}); self.assertEqual(st, 202)
        job = c.wait_job(j["job"]); self.assertEqual(job["state"], "done"); self.assertEqual(job["summary"]["done"], 2)
        self.assertTrue(os.path.exists(os.path.join(FX.sd, "Samples/Work/kick.wav"))); self.assertTrue(os.path.exists(os.path.join(FX.sd, "Samples/Work/Kit/hat.wav")))
        # conflict: fail policy -> partial job, source untouched
        st, j, _ = c.req("POST", "/api/v1/files/copy", {"items": [{"volume": "sdcard", "path": "Samples/User/kick.wav"}], "dest": {"volume": "sdcard", "path": "Samples/Work"}})
        job = c.wait_job(j["job"]); self.assertEqual(job["state"], "failed"); self.assertIn("exists", job["items"][0]["error"])
        st, j, _ = c.req("POST", "/api/v1/files/copy", {"items": [{"volume": "sdcard", "path": "Samples/User/kick.wav"}], "dest": {"volume": "sdcard", "path": "Samples/Work"}, "conflict": "rename"})
        job = c.wait_job(j["job"]); self.assertEqual(job["state"], "done"); self.assertTrue(os.path.exists(os.path.join(FX.sd, "Samples/Work/kick 2.wav")))
        # move
        st, j, _ = c.req("POST", "/api/v1/files/move", {"items": [{"volume": "sdcard", "path": "Samples/Work/kick 2.wav"}], "dest": {"volume": "sdcard", "path": "Projects"}})
        job = c.wait_job(j["job"]); self.assertEqual(job["state"], "done"); self.assertTrue(os.path.exists(os.path.join(FX.sd, "Projects/kick 2.wav"))); self.assertFalse(os.path.exists(os.path.join(FX.sd, "Samples/Work/kick 2.wav")))
        # copy into read-only volume refused up front
        st, j, _ = c.req("POST", "/api/v1/files/copy", {"items": [{"volume": "sdcard", "path": "Samples/User/kick.wav"}], "dest": {"volume": "internal-samples", "path": ""}}); self.assertEqual(st, 403)
        # delete to trash, restore
        st, j, _ = c.req("POST", "/api/v1/files/delete", {"items": [{"volume": "sdcard", "path": "Projects/kick 2.wav"}]}); self.assertEqual(st, 202)
        job = c.wait_job(j["job"]); self.assertEqual(job["state"], "done"); self.assertFalse(os.path.exists(os.path.join(FX.sd, "Projects/kick 2.wav")))
        st, j, _ = c.req("GET", "/api/v1/files/trash?volume=sdcard"); self.assertEqual(st, 200); item = [t for t in j["items"] if t["original_path"] == "Projects/kick 2.wav"][0]
        st, j, _ = c.req("POST", "/api/v1/files/trash/restore", {"volume": "sdcard", "id": item["id"]}); self.assertEqual(st, 200); self.assertTrue(os.path.exists(os.path.join(FX.sd, "Projects/kick 2.wav")))
        st, j, _ = c.req("POST", "/api/v1/files/trash/restore", {"volume": "sdcard", "id": "../../x"}); self.assertEqual(st, 400)
        # protected delete refused before any job runs
        st, j, _ = c.req("POST", "/api/v1/files/delete", {"items": [{"volume": "sdcard", "path": "autorun"}]}); self.assertEqual(st, 403)
        st, j, _ = c.req("POST", "/api/v1/files/delete", {"items": []}); self.assertEqual(st, 400)
        # jobs list and cancel semantics
        st, j, _ = c.req("GET", "/api/v1/jobs"); self.assertTrue(len(j["jobs"]) >= 4)
        st, j, _ = c.req("POST", "/api/v1/jobs/%s/cancel" % job["id"]); self.assertEqual(st, 404)   # already finished
        other = self.c(); other.pair()
        st, j, _ = other.req("GET", "/api/v1/jobs"); self.assertEqual(len(j["jobs"]), 0)            # jobs are per session
        st, j, _ = c.req("POST", "/api/v1/files/rename", {"volume": "sdcard", "path": "Projects/kick 2.wav", "name": "renamed.wav"}); self.assertEqual(st, 200)
        st, j, _ = c.req("POST", "/api/v1/files/rename", {"volume": "sdcard", "path": "Projects/renamed.wav", "name": "bad/name"}); self.assertEqual(st, 400)

    # ---------------- uploads
    def test_chunked_upload_resume_and_commit(self):
        c = self.c(); c.pair()
        payload = make_wav(300000); sha = hashlib.sha256(payload).hexdigest()   # ~600 KB: several chunks
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"volume": "sdcard", "dir": "Samples/User", "name": "upload.wav", "size": len(payload), "sha256": sha}); self.assertEqual(st, 201)
        uid = j["id"]; chunk = j["chunk_max"]
        st, j, _ = c.req("PUT", "/api/v1/transfers/upload/%s?offset=0" % uid, payload[:chunk], raw=True, headers={"Content-Type": "application/octet-stream"}); self.assertEqual(st, 200); self.assertEqual(j["upload"]["received"], chunk)
        st, j, _ = c.req("PUT", "/api/v1/transfers/upload/%s?offset=0" % uid, payload[:chunk], raw=True); self.assertEqual(st, 400)   # wrong offset -> resume info
        self.assertEqual(j["upload"]["received"], chunk)
        off = chunk
        while off < len(payload):
            st, j, _ = c.req("PUT", "/api/v1/transfers/upload/%s?offset=%d" % (uid, off), payload[off:off + chunk], raw=True); self.assertEqual(st, 200); off += chunk
        st, j, _ = c.req("POST", "/api/v1/transfers/upload/%s/commit" % uid, {}); self.assertEqual(st, 200); self.assertEqual(j["path"], "Samples/User/upload.wav"); self.assertEqual(j["sha256"], sha)
        with open(os.path.join(FX.sd, "Samples/User/upload.wav"), "rb") as f: self.assertEqual(f.read(), payload)
        # second upload with same name: fail by default, rename on request
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"volume": "sdcard", "dir": "Samples/User", "name": "upload.wav", "size": 3}); uid2 = j["id"]
        c.req("PUT", "/api/v1/transfers/upload/%s?offset=0" % uid2, b"abc", raw=True)
        st, j, _ = c.req("POST", "/api/v1/transfers/upload/%s/commit" % uid2, {}); self.assertEqual(st, 409)
        st, j, _ = c.req("POST", "/api/v1/transfers/upload/%s/commit" % uid2, {"conflict": "rename"}); self.assertEqual(st, 200); self.assertEqual(j["path"], "Samples/User/upload 2.wav")
        # invalid requests
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"volume": "sdcard", "dir": "autorun", "name": "x.sh", "size": 3}); self.assertEqual(st, 403)
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"volume": "internal-samples", "dir": "", "name": "x.wav", "size": 3}); self.assertEqual(st, 403)
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"volume": "sdcard", "dir": "", "name": "../x", "size": 3}); self.assertEqual(st, 400)
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"volume": "sdcard", "dir": "", "name": "x", "size": 10 ** 15}); self.assertIn(st, (400, 507))
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"volume": "sdcard", "dir": "", "name": "x", "size": "big"}); self.assertEqual(st, 400)
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"volume": "sdcard", "dir": "", "name": "x.wav", "size": 2}); uid3 = j["id"]
        st, j, _ = c.req("PUT", "/api/v1/transfers/upload/%s?offset=0" % uid3, b"a" * (65536 * 4 + 1), raw=True); self.assertIn(st, (400, 413))
        st, j, _ = c.req("DELETE", "/api/v1/transfers/upload/" + uid3); self.assertEqual(st, 200)
        other = self.c(); other.pair()
        st, j, _ = other.req("GET", "/api/v1/transfers/upload/" + uid); self.assertEqual(st, 404)   # not visible to another session
        self.assertEqual([f for f in os.listdir(os.path.join(FX.appdata, "staging")) if f.endswith(".part") and uid3 in f], [])

    # ---------------- inbox and intelligence
    def test_inbox_flow(self):
        c = self.c(); c.pair()
        payload = make_wav(400)
        st, j, _ = c.req("POST", "/api/v1/transfers/upload", {"inbox": True, "name": "new hat.wav", "size": len(payload)}); self.assertEqual(st, 201); uid = j["id"]
        c.req("PUT", "/api/v1/transfers/upload/%s?offset=0" % uid, payload, raw=True)
        st, j, _ = c.req("POST", "/api/v1/transfers/upload/%s/commit" % uid, {}); self.assertEqual(st, 200); item = j["inbox_item"]
        st, j, _ = c.req("GET", "/api/v1/inbox"); self.assertTrue(any(i["id"] == item for i in j["items"]))
        st, j, _ = c.req("GET", "/api/v1/inbox/" + item); self.assertTrue(j["wav"]["supported"]); self.assertEqual(j["kind"], "audio")
        st, j, _ = c.req("GET", "/api/v1/inbox/%s/peaks?buckets=16" % item); self.assertEqual(len(j["peaks"]), 32)
        st, body, hd = c.req("GET", "/api/v1/inbox/%s/audio" % item, decode=False); self.assertEqual(body, payload)
        st, j, _ = c.req("POST", "/api/v1/inbox/%s/rename" % item, {"name": "Hat Closed.wav"}); self.assertEqual(st, 200)
        st, j, _ = c.req("POST", "/api/v1/inbox/%s/commit" % item, {"volume": "sdcard", "path": "autorun"}); self.assertEqual(st, 403)
        st, j, _ = c.req("POST", "/api/v1/inbox/%s/commit" % item, {"volume": "sdcard", "path": "Samples/Kit"}); self.assertEqual(st, 200); self.assertEqual(j["path"], "Samples/Kit/Hat Closed.wav")
        self.assertTrue(os.path.exists(os.path.join(FX.sd, "Samples/Kit/Hat Closed.wav")))
        st, j, _ = c.req("GET", "/api/v1/inbox/" + item); self.assertEqual(st, 404)

    def test_search_index_duplicates_collections(self):
        c = self.c(); c.pair()
        st, j, _ = c.req("POST", "/api/v1/search/index", {"volume": "sdcard"}); self.assertEqual(st, 202); job = c.wait_job(j["job"]); self.assertEqual(job["state"], "done")
        st, j, _ = c.req("GET", "/api/v1/search?q=kick&kind=audio"); self.assertEqual(st, 200); self.assertGreaterEqual(j["total"], 1)
        st, j, _ = c.req("GET", "/api/v1/search?q=autorun"); self.assertEqual(j["total"], 0)   # protected paths are never indexed
        st, j, _ = c.req("POST", "/api/v1/search/duplicates", {"volume": "sdcard"}); job = c.wait_job(j["job"]); self.assertEqual(job["state"], "done")
        self.assertTrue(any(len(g["files"]) >= 2 for g in job["detail"]["groups"]))   # kick.wav copies
        st, j, _ = c.req("POST", "/api/v1/collections", {"name": "Favourites"}); self.assertEqual(st, 200); cid = j["id"]
        st, j, _ = c.req("POST", "/api/v1/collections/%s/items" % cid, {"volume": "sdcard", "path": "Samples/User/kick.wav"}); self.assertEqual(st, 200)
        st, j, _ = c.req("GET", "/api/v1/collections"); self.assertEqual(len([x for x in j["collections"] if x["id"] == cid][0]["items"]), 1)
        st, j, _ = c.req("POST", "/api/v1/favorites", {"volume": "sdcard", "path": "Samples/User/snare.wav", "on": True}); self.assertEqual(st, 200)
        st, j, _ = c.req("POST", "/api/v1/tags", {"volume": "sdcard", "path": "Samples/User/snare.wav", "tags": ["drum", "snare"]}); self.assertEqual(st, 200)
        st, j, _ = c.req("GET", "/api/v1/files/stat?volume=sdcard&path=Samples/User/snare.wav"); self.assertEqual(j["tags"], ["drum", "snare"])
        st, j, _ = c.req("POST", "/api/v1/files/batch_rename/plan", {"volume": "sdcard", "dir": "Samples/User", "names": ["kick.wav", "snare.wav"], "pattern": "Drum {n2}{ext}"}); self.assertEqual(j["plan"][1]["to"], "Drum 02.wav")
        st, j, _ = c.req("GET", "/api/v1/storage/status"); self.assertEqual(st, 200); self.assertTrue(j["volumes"][0]["counts"]["indexed"]); self.assertFalse(j["media_health"]["supported"])
        st, j, _ = c.req("GET", "/api/v1/storage/network"); self.assertEqual(st, 200); self.assertIn("interfaces", j)
        st, j, _ = c.req("GET", "/api/v1/storage/journal"); self.assertTrue(len(j["entries"]) > 0)

    # ---------------- screen
    def test_screen_frames_are_labelled_simulated(self):
        c = self.c(); c.pair(role="viewer")
        st, j, _ = c.req("GET", "/api/v1/screen/status"); self.assertEqual(st, 200); self.assertTrue(j["simulated"]); self.assertFalse(j["remote_input"]["enabled"])
        st, body, hd = c.req("GET", "/api/v1/screen/frame", decode=False); self.assertEqual(st, 200)
        self.assertEqual(len(body), 320 * 240 * 2); self.assertEqual(hd["x-dm-frame-simulated"], "1"); self.assertEqual(hd["x-dm-frame-size"], "320x240")
        st, j, hd = c.req("GET", "/api/v1/screen/frame"); self.assertEqual(st, 429)   # faster than max fps
        fm = self.c(); fm.pair()
        st, j, _ = fm.req("POST", "/api/v1/screen/input", {"control": "pad1"}); self.assertEqual(st, 501)

    # ---------------- format simulator
    def test_format_simulation_gates(self):
        c = self.c(); c.pair(); c.req("POST", "/api/v1/auth/elevate", {"password": ADMIN_PW})
        st, j, _ = c.req("GET", "/api/v1/admin/format/devices"); self.assertEqual(st, 200); self.assertTrue(j["simulation"])
        st, j, _ = c.req("POST", "/api/v1/admin/format/preflight", {"volume": "internal-samples"}); self.assertEqual(st, 200); self.assertFalse(j["eligible"]); self.assertNotIn("token", j)
        self.assertFalse([x for x in j["checks"] if x["name"] == "not_internal_storage"][0]["pass"])
        # the temp-dir backed "sdcard" has no real removable block device -> preflight must refuse
        st, j, _ = c.req("POST", "/api/v1/admin/format/preflight", {"volume": "sdcard"}); self.assertEqual(st, 200); self.assertFalse(j["eligible"]); self.assertTrue(j["simulation"])
        self.assertFalse([x for x in j["checks"] if x["name"] == "device_is_removable_sd"][0]["pass"])
        st, j, _ = c.req("POST", "/api/v1/admin/format/confirm", {"token": "x", "volume": "sdcard", "device": "/dev/mmcblk1", "confirm_erase": True}); self.assertEqual(st, 403)
        st, j, _ = c.req("POST", "/api/v1/admin/format/confirm", {"token": "x", "volume": "sdcard", "device": "/dev/mmcblk1"}); self.assertEqual(st, 400)
        st, j, _ = c.req("POST", "/api/v1/admin/format/preflight", {"volume": "../sdcard"}); self.assertEqual(st, 400)
        st, j, _ = c.req("POST", "/api/v1/admin/format/preflight", {"volume": "ghost"}); self.assertFalse(j["eligible"])

    def test_static_frontend_served(self):
        c = self.c()
        st, body, hd = c.req("GET", "/", decode=False); self.assertEqual(st, 200); self.assertIn(b"DISKMODE", body)
        st, body, hd = c.req("GET", "/app.js", decode=False); self.assertEqual(st, 200); self.assertIn("javascript", hd["content-type"])
        st, body, hd = c.req("GET", "/../etc/passwd", decode=False); self.assertNotEqual(st, 200)

    def test_concurrent_requests(self):
        import threading
        c = self.c(); c.pair(); errors = []
        def worker(n):
            try:
                cc = Client(FX.port); cc.cookie = c.cookie; cc.csrf = c.csrf
                for _ in range(5):
                    st, j, _ = cc.req("GET", "/api/v1/files?volume=sdcard&path=Samples/User")
                    if st != 200: errors.append(st)
                st, j, _ = cc.req("POST", "/api/v1/files/mkdir", {"volume": "sdcard", "path": "conc-%d" % n})
                if st != 200: errors.append(("mkdir", st))
            except Exception as e: errors.append(repr(e))
        ts = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
        [t.start() for t in ts]; [t.join() for t in ts]
        self.assertEqual(errors, [])


if __name__ == "__main__":
    sys.argv = sys.argv[:1]
    unittest.main(verbosity=1)
