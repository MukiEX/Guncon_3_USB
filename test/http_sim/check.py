import json, sys, urllib.request, urllib.parse
B = "http://192.168.5.1"
def get(p): return json.loads(urllib.request.urlopen(B + p, timeout=3).read())
def post(p, d): return json.loads(urllib.request.urlopen(B + p, data=urllib.parse.urlencode(d).encode(), timeout=3).read())
fail = 0
for ep in ["/api/settings", "/api/state", "/api/diag"]:
    try: get(ep); print("OK   valid JSON", ep)
    except Exception as e: print("FAIL", ep, e); fail = 1
s0 = get("/api/settings")
need = ["pads","padType","aimParkAtBoot","playerMode","portMap","map","calib","smoothing","custom"]
miss = [k for k in need if k not in s0]
print("OK   all keys present" if not miss else f"FAIL missing {miss}"); fail |= bool(miss)
print("OK   map has 9 entries" if len(s0["map"]) == 9 else f"FAIL map {s0['map']}"); fail |= len(s0["map"]) != 9
post("/api/settings", {"map1": 6, "pads": 3, "padType": 0, "pm2": 0x23})
s1 = get("/api/settings")
ok = s1["map"][1] == 6 and s1["pads"] == 3 and s1["padType"] == 0 and s1["portMap"][2] == 0x23 and s1["map"][0] == s0["map"][0] and s1["smoothing"] == s0["smoothing"]
print("OK   save round-trip, untouched fields kept" if ok else f"FAIL round-trip {s1}"); fail |= not ok
post("/api/aim", {"parked": 1}); st = get("/api/state")
print("OK   aim park reflected in state" if st["aimParked"] == 1 else "FAIL aim park"); fail |= st["aimParked"] != 1
d = get("/api/settings")
print("OK   defaults: XInput" if s0["padType"] == 1 else f"FAIL default padType {s0['padType']}"); fail |= s0["padType"] != 1
sys.exit(fail)
