import argparse
import bz2
import concurrent.futures
import gzip
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
import zipfile
from collections import Counter
from pathlib import Path
from typing import Any

import requests
from tqdm import tqdm

from dataset import PINNED_TEST_IDS

LEAK_WINDOW_SECONDS = 3 * 3600
USER_AGENT = "UniboxikML/1.0"
TF2_MAP_DIRS = [
    Path(r"C:\SteamLibrary\steamapps\common\Team Fortress 2\tf\maps"),
    Path(r"C:\SteamLibrary\steamapps\common\Team Fortress 2\tf\download\maps"),
]
FASTDL_BASES = [
    "https://fastdl.serveme.tf/maps/",
    "https://maps.demos.tf/",
]
TARGET_MAP_PREFIXES = [
    "koth_ultiduo",
    "ultiduo_baloo",
    "ctf_ballin",
    "ctf_bball",
    "mge_training",
    "cp_process",
    "cp_snakewater",
    "cp_gullywash",
    "cp_sunshine",
    "cp_metalworks",
    "koth_product",
    "koth_bagel",
    "pl_upward",
    "pl_badwater",
    "cp_granary",
    "cp_badlands",
    "koth_viaduct",
    "cp_reckoner",
    "cp_sultry",
    "cp_villa",
    "pl_vigil",
    "cp_steel",
    "koth_ashville",
    "koth_clearcut",
    "pl_swiftwater",
    "koth_proot",
    "koth_cascade",
    "pl_borneo",
    "koth_lakeside",
]
AIR_HEAVY_PREFIXES = {"koth_ultiduo", "ultiduo_baloo", "ctf_ballin", "ctf_bball", "mge_training"}
UNSEEN_TEST_FAMILIES = {"koth_bagel", "cp_metalworks"}
RGL_HIGH_DIVISIONS = ("invite", "advanced", "main", "challenger", "div-1", "div 1")
ETF2L_LOW_KEYWORDS = ("fun", "fresh", "open", "low", "division 3", "division 4", "3rd place", "mid", "preseason", "nations")


def make_session() -> requests.Session:
    s = requests.Session()
    s.headers.update({"User-Agent": USER_AGENT})
    return s


def fetch_json(session: requests.Session, url: str, timeout: int = 15) -> Any:
    for attempt in range(3):
        try:
            r = session.get(url, timeout=timeout)
            if r.status_code == 429:
                time.sleep(2.0 * (attempt + 1))
                continue
            r.raise_for_status()
            return r.json()
        except Exception:
            if attempt == 2:
                return None
            time.sleep(0.8 * (attempt + 1))
    return None


def fetch_text(session: requests.Session, url: str, timeout: int = 15) -> str | None:
    for attempt in range(3):
        try:
            r = session.get(url, timeout=timeout)
            if r.status_code == 429:
                time.sleep(2.0 * (attempt + 1))
                continue
            r.raise_for_status()
            return r.text
        except Exception:
            if attempt == 2:
                return None
            time.sleep(0.8 * (attempt + 1))
    return None


def normalize_map_family(map_name: str) -> str:
    m = map_name.lower().strip()
    for prefix in TARGET_MAP_PREFIXES:
        if m.startswith(prefix):
            return prefix
    parts = m.split("_")
    return "_".join(parts[:2]) if len(parts) >= 2 else m


def resolve_bsp(
    session: requests.Session,
    map_name: str,
    maps_dir: Path,
    neg_cache: set[str],
    allow_download: bool = True,
) -> Path | None:
    if not map_name:
        return None
    clean = map_name.strip().lower()
    if "/" in clean or "\\" in clean or ".." in clean:
        return None
    local_custom = maps_dir / f"{clean}.bsp"
    if local_custom.exists() and local_custom.stat().st_size > 1024:
        return local_custom
    for d in TF2_MAP_DIRS:
        cand = d / f"{clean}.bsp"
        if cand.exists() and cand.stat().st_size > 1024:
            return cand
    if clean in neg_cache:
        return None
    if not allow_download:
        for base in FASTDL_BASES:
            for ext in (".bsp", ".bsp.bz2"):
                try:
                    r = session.head(f"{base}{clean}{ext}", timeout=6, allow_redirects=True)
                    if r.status_code == 200:
                        return local_custom
                except Exception:
                    pass
        neg_cache.add(clean)
        return None

    maps_dir.mkdir(parents=True, exist_ok=True)
    for base in FASTDL_BASES:
        for ext in (".bsp", ".bsp.bz2"):
            url = f"{base}{clean}{ext}"
            try:
                with session.get(url, stream=True, timeout=60) as r:
                    if r.status_code != 200:
                        continue
                    tmp = maps_dir / f"{clean}{ext}.part"
                    with open(tmp, "wb") as f:
                        for chunk in r.iter_content(chunk_size=1024 * 256):
                            if chunk:
                                f.write(chunk)
                    if ext == ".bsp.bz2":
                        raw = bz2.decompress(tmp.read_bytes())
                        tmp.unlink(missing_ok=True)
                        if not raw.startswith(b"VBSP"):
                            continue
                        local_custom.write_bytes(raw)
                    else:
                        with open(tmp, "rb") as chk:
                            hdr = chk.read(4)
                        if hdr != b"VBSP":
                            tmp.unlink(missing_ok=True)
                            continue
                        tmp.replace(local_custom)
                    return local_custom
            except Exception:
                continue
    neg_cache.add(clean)
    return None


def collect_etf2l(session: requests.Session, max_comps: int = 40, max_matches_per_comp: int = 14) -> tuple[list[dict], dict[str, dict]]:
    players: dict[str, dict] = {}
    direct_demos: list[dict] = []
    comp_ids: list[tuple[int, str, str]] = []
    for page in range(1, 22):
        data = fetch_json(session, f"https://api-v2.etf2l.org/competition/list?limit=50&page={page}")
        if not data:
            break
        comps = data.get("competitions", {}).get("data", [])
        if not comps:
            break
        for c in comps:
            ctype = c.get("type", "")
            cname = c.get("name", "")
            if ctype not in ("6v6", "Highlander"):
                continue
            nl = cname.lower()
            if "season" not in nl and "premiership" not in nl:
                continue
            if any(k in nl for k in ETF2L_LOW_KEYWORDS):
                continue
            comp_ids.append((int(c["id"]), cname, ctype))
        if len(comp_ids) >= max_comps:
            break

    for cid, cname, ctype in comp_ids[:max_comps]:
        res = fetch_json(session, f"https://api-v2.etf2l.org/competition/{cid}/results?limit=100")
        if not res:
            continue
        rows = res.get("results", {}).get("data", [])
        matches = [
            m for m in rows
            if not m.get("defaultwin") and m.get("id") and (m.get("division") or {}).get("tier") in (0, 1, 2)
        ]
        step = max(1, len(matches) // max_matches_per_comp)
        mids = [m["id"] for m in matches[::step][:max_matches_per_comp]]

        def fetch_match(mid: int):
            time.sleep(0.15)
            return mid, fetch_json(make_session(), f"https://api-v2.etf2l.org/matches/{mid}")

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
            for mid, md_wrap in ex.map(fetch_match, mids):
                if not md_wrap or "match" not in md_wrap:
                    continue
                md = md_wrap["match"]
                div_name = (md.get("division") or {}).get("name", "Unknown")
                div_tier = (md.get("division") or {}).get("tier")
                for p in md.get("players") or []:
                    id64 = ((p.get("steam") or {}).get("id64") or "").strip()
                    if id64 and id64 not in players:
                        players[id64] = {
                            "league": "ETF2L",
                            "tier": div_name,
                            "tier_num": div_tier,
                            "format": ctype,
                            "competition": cname,
                        }
                for d in md.get("demos") or []:
                    if d.get("pruned"):
                        continue
                    durl = d.get("download_url")
                    if not durl:
                        continue
                    direct_demos.append({
                        "source": "etf2l_direct",
                        "league": "ETF2L",
                        "tier": div_name,
                        "format": ctype,
                        "competition": cname,
                        "match_id": mid,
                        "maps": md.get("maps") or [],
                        "time": d.get("time") or md.get("time") or 0,
                        "is_pov": bool(d.get("first_person")),
                        "url": durl,
                    })
    return direct_demos, players


def collect_rgl_players(session: requests.Session, season_ids: list[int], max_teams_per_season: int = 8) -> dict[str, dict]:
    players: dict[str, dict] = {}
    for sid in season_ids:
        s = fetch_json(session, f"https://api.rgl.gg/v0/seasons/{sid}")
        if not s:
            continue
        fmt = s.get("formatName", "Sixes")
        sname = s.get("name", f"RGL Season {sid}")
        teams = s.get("participatingTeams") or []

        def fetch_team(tid: int):
            return fetch_json(make_session(), f"https://api.rgl.gg/v0/teams/{tid}", timeout=10)

        checked = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
            futs = [ex.submit(fetch_team, tid) for tid in teams]
            for fut in concurrent.futures.as_completed(futs):
                t = fut.result()
                if checked >= max_teams_per_season:
                    for f in futs:
                        f.cancel()
                    break
                if not t:
                    continue
                div = (t.get("divisionName") or "").strip()
                if not any(k in div.lower() for k in RGL_HIGH_DIVISIONS):
                    continue
                checked += 1
                for p in t.get("players") or []:
                    id64 = str(p.get("steamId") or "").strip()
                    if id64 and id64 not in players:
                        players[id64] = {
                            "league": "RGL",
                            "tier": div,
                            "format": "6v6" if "six" in fmt.lower() else fmt,
                            "competition": sname,
                        }
    return players


def collect_ozfortress_players(session: requests.Session, league_ids: list[int], max_users: int = 75) -> dict[str, dict]:
    players: dict[str, dict] = {}
    user_ids: list[str] = []
    for lid in league_ids:
        html = fetch_text(session, f"https://ozfortress.com/leagues/{lid}")
        if not html:
            continue
        team_paths = sorted(set(re.findall(r"/teams/\d+", html)))
        for tp in team_paths[:16]:
            th = fetch_text(session, f"https://ozfortress.com{tp}")
            if not th:
                continue
            for up in re.findall(r"/users/\d+", th):
                if up not in user_ids:
                    user_ids.append(up)
                    if len(user_ids) >= max_users:
                        break
            if len(user_ids) >= max_users:
                break
        if len(user_ids) >= max_users:
            break

    def fetch_user(up: str):
        return fetch_text(make_session(), f"https://ozfortress.com{up}", timeout=10)

    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
        for uh in ex.map(fetch_user, user_ids[:max_users]):
            if not uh:
                continue
            ids = re.findall(r"7656119\d{10}", uh)
            if ids:
                id64 = ids[0]
                if id64 not in players:
                    players[id64] = {
                        "league": "ozfortress",
                        "tier": "Premier/Main",
                        "format": "6v6",
                        "competition": "ozfortress",
                    }
    return players


def _steamid3_to_64(s: str) -> str | None:
    m = re.match(r"^\[U:1:(\d+)\]$", s.strip())
    if not m:
        return None
    return str(76561197960265728 + int(m.group(1)))


def collect_logstf_data(session: requests.Session, pages: int = 6) -> tuple[dict[tuple[str, int], dict], dict[str, dict]]:
    index: dict[tuple[str, int], dict] = {}
    players: dict[str, dict] = {}
    log_ids: list[tuple[int, str]] = []
    for p in range(pages):
        data = fetch_json(session, f"https://logs.tf/api/v1/log?limit=100&offset={p * 100}")
        if not data or not data.get("logs"):
            break
        for row in data["logs"]:
            m = (row.get("map") or "").lower()
            dt = int(row.get("date") or 0)
            if m and dt:
                index[(m, dt // 600)] = row
    for mp in ("koth_ultiduo", "ultiduo_baloo", "cp_process_f12", "cp_snakewater_final1", "cp_sunshine", "koth_product_final"):
        data = fetch_json(session, f"https://logs.tf/api/v1/log?map={mp}&limit=10")
        if data and data.get("logs"):
            for row in data["logs"][:8]:
                lid = row.get("id")
                if lid:
                    log_ids.append((int(lid), mp))
                    m = (row.get("map") or "").lower()
                    dt = int(row.get("date") or 0)
                    if m and dt:
                        index[(m, dt // 600)] = row

    def fetch_log(item: tuple[int, str]):
        lid, mp = item
        return mp, fetch_json(make_session(), f"https://logs.tf/api/v1/log/{lid}", timeout=10)

    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
        for mp, lj in ex.map(fetch_log, log_ids):
            if not lj or not isinstance(lj.get("players"), dict):
                continue
            for sid3, pdata in lj["players"].items():
                id64 = _steamid3_to_64(sid3)
                if not id64 or id64 in players:
                    continue
                if int(pdata.get("dapm") or 0) < 180:
                    continue
                players[id64] = {
                    "league": "logs.tf",
                    "tier": "competitive",
                    "format": "Ultiduo" if "ultiduo" in mp else "6v6",
                    "competition": mp,
                }
    return index, players


def make_demostf_record(d: dict, source: str, league: str, tier: str, fmt: str) -> dict | None:
    url = d.get("url")
    if not url:
        return None
    m = (d.get("map") or "").strip().lower()
    if not m:
        return None
    fam = normalize_map_family(m)
    dur = int(d.get("duration") or 0)
    pc = int(d.get("playerCount") or 0)
    if fam == "mge_training":
        if dur < 240 or dur > 7200 or pc < 2:
            return None
        if fmt == "unknown":
            fmt = "MGE"
    elif fam in ("koth_ultiduo", "ultiduo_baloo"):
        if dur < 300 or dur > 7200 or pc < 4:
            return None
        if fmt == "unknown":
            fmt = "Ultiduo"
    elif fam in ("ctf_ballin", "ctf_bball"):
        if dur < 300 or dur > 7200 or pc < 4:
            return None
        if fmt == "unknown":
            fmt = "BBall"
    else:
        if dur < 600 or dur > 7200 or pc < 12:
            return None
        if fmt == "unknown":
            fmt = "Highlander" if pc >= 18 else "6v6"
    nick = (d.get("nick") or "").strip()
    is_pov = nick != "SourceTV Demo"
    return {
        "id": f"demostf_{d['id']}",
        "demostf_id": int(d["id"]),
        "source": source,
        "league": league,
        "tier": tier,
        "format": fmt,
        "map": m,
        "map_family": fam,
        "duration": dur,
        "player_count": pc,
        "is_pov": is_pov,
        "nick": nick,
        "server": d.get("server") or "",
        "time": int(d.get("time") or 0),
        "hash": d.get("hash") or "",
        "url": url,
    }


def query_demostf_by_players(
    session: requests.Session,
    players: dict[str, dict],
    max_queries: int = 80,
) -> dict[str, dict]:
    candidates: dict[str, dict] = {}
    items = list(players.items())[:max_queries]

    def fetch_player(item: tuple[str, dict]):
        id64, meta = item
        return id64, meta, fetch_json(make_session(), f"https://api.demos.tf/demos?players[]={id64}")

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        for _, meta, rows in ex.map(fetch_player, items):
            if not isinstance(rows, list):
                continue
            for d in rows:
                rec = make_demostf_record(
                    d,
                    source=f"{meta['league'].lower()}+demos.tf",
                    league=meta["league"],
                    tier=meta["tier"],
                    fmt=meta["format"],
                )
                if rec:
                    key = rec.get("hash") or rec["id"]
                    if key not in candidates:
                        candidates[key] = rec
    return candidates


def query_demostf_by_maps(
    session: requests.Session,
    map_prefixes: list[str],
    pages_per_map: int = 8,
    pov_scan_pages: int = 40,
    type_pov_pages: int = 24,
) -> dict[str, dict]:
    candidates: dict[str, dict] = {}
    urls = [
        ("map", f"https://api.demos.tf/demos?map={mp}&page={page}")
        for mp in map_prefixes
        for page in range(1, pages_per_map + 1)
    ] + [
        ("pov", f"https://api.demos.tf/demos?page={page}")
        for page in range(1, pov_scan_pages + 1)
    ] + [
        ("pov", f"https://api.demos.tf/demos?type=pov&page={page}")
        for page in range(1, type_pov_pages + 1)
    ]

    def fetch_page(item: tuple[str, str]):
        kind, url = item
        return kind, fetch_json(make_session(), url)

    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as ex:
        for kind, rows in ex.map(fetch_page, urls):
            if not isinstance(rows, list):
                continue
            for d in rows:
                if kind == "pov" and (d.get("nick") or "").strip() == "SourceTV Demo":
                    continue
                rec = make_demostf_record(d, "demos.tf", "demos.tf", "competitive", "unknown")
                if rec:
                    key = rec.get("hash") or rec["id"]
                    if key not in candidates:
                        candidates[key] = rec
    return candidates


def select_balanced_demos(
    candidates: list[dict],
    target_stv: int,
    target_pov: int,
    max_per_map_stv: int = 16,
    max_per_map_pov: int = 12,
) -> list[dict]:
    def score(r: dict) -> tuple:
        league_prio = 0 if r["league"] in ("ETF2L", "RGL", "ozfortress", "logs.tf") else 1
        tier_l = r["tier"].lower()
        tier_prio = 0 if ("prem" in tier_l or "invite" in tier_l) else (1 if "div" in tier_l or "adv" in tier_l or "high" in tier_l or "comp" in tier_l else 2)
        fam_prio = 0 if r["map_family"] in TARGET_MAP_PREFIXES[:18] else 1
        return (league_prio, tier_prio, fam_prio, -r["time"])

    povs = sorted([c for c in candidates if c["is_pov"]], key=score)
    stvs = sorted([c for c in candidates if not c["is_pov"]], key=score)

    selected: list[dict] = []
    selected_ids: set[str] = set()
    pov_counts: Counter = Counter()
    stv_counts: Counter = Counter()

    def pick_round_robin(pool: list[dict], target: int, counts: Counter, max_cap: int) -> None:
        if target <= 0:
            return
        picked_start = len(selected)
        by_fam: dict[str, list[dict]] = {}
        for c in pool:
            by_fam.setdefault(c["map_family"], []).append(c)
        fams = [f for f in TARGET_MAP_PREFIXES if f in by_fam] + [f for f in by_fam if f not in TARGET_MAP_PREFIXES]
        idx_map = {f: 0 for f in fams}
        for cap in range(1, max_cap + 1):
            progressed = True
            while progressed and (len(selected) - picked_start) < target:
                progressed = False
                for f in fams:
                    if (len(selected) - picked_start) >= target:
                        break
                    if counts[f] >= cap:
                        continue
                    lst = by_fam[f]
                    while idx_map[f] < len(lst) and lst[idx_map[f]]["id"] in selected_ids:
                        idx_map[f] += 1
                    if idx_map[f] < len(lst):
                        cand = lst[idx_map[f]]
                        idx_map[f] += 1
                        selected.append(cand)
                        selected_ids.add(cand["id"])
                        counts[f] += 1
                        progressed = True

    pick_round_robin(povs, target_pov, pov_counts, max_per_map_pov * 2)
    pick_round_robin(stvs, target_stv, stv_counts, max_per_map_stv * 2)
    return selected


def download_demo_file(session: requests.Session, rec: dict, demos_dir: Path) -> Path | None:
    demos_dir.mkdir(parents=True, exist_ok=True)
    kind = "pov" if rec["is_pov"] else "stv"
    safe_map = re.sub(r"[^a-zA-Z0-9_.-]", "_", rec["map"])
    out_path = demos_dir / f"{rec['id']}_{kind}_{safe_map}.dem"
    if out_path.exists() and out_path.stat().st_size >= 1_000_000:
        with open(out_path, "rb") as f:
            if f.read(8) == b"HL2DEMO\x00":
                return out_path

    url = rec["url"]
    tmp_path = demos_dir / f"{rec['id']}.download.part"
    try:
        with session.get(url, stream=True, timeout=90, allow_redirects=True) as r:
            if r.status_code != 200:
                return None
            cl = r.headers.get("Content-Length")
            if cl and int(cl) < 500_000:
                return None
            final_name = r.url.split("?")[0].lower()
            with open(tmp_path, "wb") as f:
                for chunk in r.iter_content(chunk_size=1024 * 256):
                    if chunk:
                        f.write(chunk)

        if final_name.endswith(".zip"):
            with zipfile.ZipFile(tmp_path, "r") as zf:
                dem_members = [n for n in zf.namelist() if n.lower().endswith(".dem")]
                if not dem_members:
                    tmp_path.unlink(missing_ok=True)
                    return None
                dem_members.sort(key=lambda n: zf.getinfo(n).file_size, reverse=True)
                data = zf.read(dem_members[0])
            tmp_path.unlink(missing_ok=True)
            if len(data) < 1_000_000 or data[:8] != b"HL2DEMO\x00":
                return None
            out_path.write_bytes(data)
            return out_path
        if final_name.endswith(".bz2"):
            data = bz2.decompress(tmp_path.read_bytes())
            tmp_path.unlink(missing_ok=True)
            if len(data) < 1_000_000 or data[:8] != b"HL2DEMO\x00":
                return None
            out_path.write_bytes(data)
            return out_path
        if final_name.endswith(".gz"):
            data = gzip.decompress(tmp_path.read_bytes())
            tmp_path.unlink(missing_ok=True)
            if len(data) < 1_000_000 or data[:8] != b"HL2DEMO\x00":
                return None
            out_path.write_bytes(data)
            return out_path
        if final_name.endswith(".rar") or final_name.endswith(".7z"):
            tmp_path.unlink(missing_ok=True)
            return None

        if tmp_path.stat().st_size < 1_000_000:
            tmp_path.unlink(missing_ok=True)
            return None
        with open(tmp_path, "rb") as f:
            hdr = f.read(8)
        if hdr != b"HL2DEMO\x00":
            tmp_path.unlink(missing_ok=True)
            return None
        tmp_path.replace(out_path)
        return out_path
    except Exception:
        tmp_path.unlink(missing_ok=True)
        return None


def read_demo_map(path: Path) -> str:
    with open(path, "rb") as f:
        hdr = f.read(8 + 4 + 4 + 260 * 3)
    if hdr[:8] != b"HL2DEMO\x00" or len(hdr) < 8 + 8 + 780:
        return ""
    return hdr[16 + 520 : 16 + 780].split(b"\x00", 1)[0].decode("utf-8", "ignore").strip().lower()


def download_etf2l_demo(session: requests.Session, d: dict, demos_dir: Path) -> tuple[Path, str] | None:
    demos_dir.mkdir(parents=True, exist_ok=True)
    num = d["url"].rstrip("/").split("/")[-1]
    kind = "pov" if d["is_pov"] else "stv"
    tmp_dir = demos_dir / f"etf2l_{num}.tmp"
    tmp_dir.mkdir(exist_ok=True)
    try:
        with session.get(d["url"], stream=True, timeout=120, allow_redirects=True) as r:
            if r.status_code != 200:
                return None
            fname = r.url.split("?")[0].split("/")[-1]
            arc = tmp_dir / fname
            with open(arc, "wb") as f:
                for chunk in r.iter_content(chunk_size=1024 * 256):
                    if chunk:
                        f.write(chunk)
        if arc.suffix.lower() != ".dem":
            if shutil.which("tar") is None:
                return None
            subprocess.run(["tar", "-xf", str(arc), "-C", str(tmp_dir)], check=True, capture_output=True, timeout=600)
            arc.unlink(missing_ok=True)
        dems = sorted(tmp_dir.rglob("*.dem"), key=lambda p: p.stat().st_size, reverse=True)
        if not dems or dems[0].stat().st_size < 1_000_000:
            return None
        map_name = read_demo_map(dems[0])
        if not map_name:
            return None
        out = demos_dir / f"etf2l_{num}_{kind}_{map_name}.dem"
        shutil.move(str(dems[0]), out)
        return out, map_name
    except Exception:
        return None
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def file_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, default=Path("ml_pipeline/data"))
    parser.add_argument("--target-stv", type=int, default=240)
    parser.add_argument("--target-pov", type=int, default=95)
    parser.add_argument("--max-gb", type=float, default=24.0)
    parser.add_argument("--etf2l-max", type=int, default=40)
    parser.add_argument("--map-only", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    data_dir: Path = args.data_dir
    demos_dir = data_dir / "demos"
    maps_dir = data_dir / "maps"
    manifest_path = data_dir / "manifest.json"
    data_dir.mkdir(parents=True, exist_ok=True)

    existing_entries: list[dict] = []
    if manifest_path.exists():
        try:
            loaded = json.loads(manifest_path.read_text(encoding="utf-8"))
            if isinstance(loaded, dict) and "demos" in loaded:
                existing_entries = loaded["demos"]
            elif isinstance(loaded, list):
                existing_entries = loaded
        except Exception:
            existing_entries = []

    existing_by_id = {e["id"]: e for e in existing_entries if Path(e.get("local_path", "")).exists()}
    existing_urls = {e.get("url", "") for e in existing_by_id.values() if e.get("url")}
    session = make_session()

    etf2l_direct, etf2l_players, rgl_players, oz_players, logs_index, logs_players = [], {}, {}, {}, {}, {}
    if not args.map_only:
        print("Querying ETF2L competitions and verified high-div rosters...", flush=True)
        etf2l_direct, etf2l_players = collect_etf2l(session)
        print(f"  ETF2L players: {len(etf2l_players)}, direct demo entries: {len(etf2l_direct)}", flush=True)

        print("Querying RGL high-div rosters...", flush=True)
        rgl_players = collect_rgl_players(session, [199, 198, 195, 194, 193, 192, 188, 187, 183], max_teams_per_season=14)
        print(f"  RGL players: {len(rgl_players)}", flush=True)

        print("Querying ozfortress rosters...", flush=True)
        oz_players = collect_ozfortress_players(session, [95, 93, 91, 89, 94, 92, 88], max_users=75)
        print(f"  ozfortress players: {len(oz_players)}", flush=True)

        print("Indexing logs.tf matches and players for cross-verification...", flush=True)
        logs_index, logs_players = collect_logstf_data(session, pages=6)
        print(f"  logs.tf index buckets: {len(logs_index)}, players: {len(logs_players)}", flush=True)

    all_players = {}
    etf_items = list(etf2l_players.items())[:130]
    rgl_items = list(rgl_players.items())[:130]
    oz_items = list(oz_players.items())[:60]
    ltf_items = list(logs_players.items())[:60]
    for k, v in etf_items + rgl_items + oz_items + ltf_items:
        all_players[k] = v

    print(f"Querying demos.tf for {len(all_players)} league-verified players...", flush=True)
    candidates_map = query_demostf_by_players(session, all_players, max_queries=380)
    print(f"  League-matched demos.tf candidates: {len(candidates_map)}", flush=True)

    print("Querying demos.tf across competitive & air-heavy maps and POV uploads...", flush=True)
    search_prefixes = [f for f in TARGET_MAP_PREFIXES if f not in UNSEEN_TEST_FAMILIES]
    map_candidates = query_demostf_by_maps(session, search_prefixes, pages_per_map=8, pov_scan_pages=40, type_pov_pages=24)
    for k, v in map_candidates.items():
        if k not in candidates_map:
            candidates_map[k] = v

    test_keys = [
        (e["map"], int(e["time"]))
        for e in existing_entries
        if e["id"] in PINNED_TEST_IDS
    ]

    def leaks_test(c: dict) -> bool:
        for tm, tt in test_keys:
            if normalize_map_family(tm) == c["map_family"] and abs(int(c["time"]) - tt) < LEAK_WINDOW_SECONDS:
                return True
        return False

    all_candidates = [
        c
        for c in candidates_map.values()
        if c["map_family"] in TARGET_MAP_PREFIXES
        and c["map_family"] not in UNSEEN_TEST_FAMILIES
        and c["id"] not in existing_by_id
        and c.get("url", "") not in existing_urls
        and not leaks_test(c)
    ]
    for c in all_candidates:
        bucket = (c["map"], c["time"] // 600)
        if bucket in logs_index:
            c["logstf_id"] = logs_index[bucket]["id"]

    neg_cache: set[str] = set()
    unique_maps = sorted({c["map"] for c in all_candidates})
    print(f"Total new deduplicated candidates: {len(all_candidates)} across {len(unique_maps)} maps. Checking BSP availability...", flush=True)

    def check_map(m: str):
        return m, resolve_bsp(make_session(), m, maps_dir, set(), allow_download=False)

    map_bsp_cache: dict[str, Path | None] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as ex:
        for m, bp in ex.map(check_map, unique_maps):
            map_bsp_cache[m] = bp

    bsp_ok_candidates: list[dict] = []
    for c in all_candidates:
        bsp_path = map_bsp_cache.get(c["map"])
        if bsp_path is not None:
            c["bsp_candidate"] = str(bsp_path)
            bsp_ok_candidates.append(c)

    pov_avail = sum(1 for c in bsp_ok_candidates if c["is_pov"])
    stv_avail = sum(1 for c in bsp_ok_candidates if not c["is_pov"])
    fam_counts = Counter(c["map_family"] for c in bsp_ok_candidates)
    print(f"BSP-verified new candidates: {len(bsp_ok_candidates)} (STV={stv_avail}, POV={pov_avail})")
    print("Map family availability:", dict(fam_counts.most_common()))

    cur_pov = sum(1 for e in existing_by_id.values() if e["is_pov"])
    cur_stv = sum(1 for e in existing_by_id.values() if not e["is_pov"])
    need_stv = max(0, args.target_stv - cur_stv)
    need_pov = max(0, args.target_pov - cur_pov)
    selected = select_balanced_demos(bsp_ok_candidates, need_stv, need_pov)
    sel_pov = sum(1 for c in selected if c["is_pov"])
    sel_stv = sum(1 for c in selected if not c["is_pov"])
    sel_fams = Counter(c["map_family"] for c in selected)
    sel_leagues = Counter(c["league"] for c in selected)
    print(f"Existing={len(existing_by_id)} (STV={cur_stv}, POV={cur_pov}) + Selected new={len(selected)} (STV={sel_stv}, POV={sel_pov})")
    print("Selected map families:", dict(sel_fams.most_common()))
    print("Selected leagues:", dict(sel_leagues.most_common()))

    if args.dry_run:
        return

    sel_maps = sorted({c["map"] for c in selected})
    resolved_maps: dict[str, Path] = {}
    for m in sel_maps:
        bp = resolve_bsp(session, m, maps_dir, neg_cache, allow_download=True)
        if bp is not None:
            resolved_maps[m] = bp

    download_queue = [c for c in selected if c["map"] in resolved_maps]
    max_bytes = int(args.max_gb * 1024 * 1024 * 1024)
    total_bytes = sum(Path(e["local_path"]).stat().st_size for e in existing_by_id.values())
    manifest_demos: dict[str, dict] = dict(existing_by_id)

    import threading
    lock = threading.Lock()
    stop_flag = threading.Event()

    def worker(rec: dict) -> dict | None:
        if stop_flag.is_set():
            return None
        with lock:
            if total_bytes >= max_bytes:
                stop_flag.set()
                return None
        dem_path = download_demo_file(make_session(), rec, demos_dir)
        if dem_path is None:
            return None
        sz = dem_path.stat().st_size
        sha = file_sha256(dem_path)
        entry = {
            "id": rec["id"],
            "source": rec["source"],
            "league": rec["league"],
            "tier": rec["tier"],
            "format": rec["format"],
            "map": rec["map"],
            "map_family": rec["map_family"],
            "duration": rec["duration"],
            "player_count": rec["player_count"],
            "is_pov": rec["is_pov"],
            "nick": rec["nick"],
            "server": rec["server"],
            "time": rec["time"],
            "url": rec["url"],
            "local_path": str(dem_path),
            "bsp_path": str(resolved_maps[rec["map"]]),
            "size_bytes": sz,
            "sha256": sha,
        }
        if "logstf_id" in rec:
            entry["logstf_id"] = rec["logstf_id"]
        return entry

    pbar = tqdm(total=len(download_queue), desc="Downloading demos")
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        for entry in ex.map(worker, download_queue):
            pbar.update(1)
            if entry is None:
                continue
            with lock:
                manifest_demos[entry["id"]] = entry
                total_bytes += entry["size_bytes"]
                if total_bytes >= max_bytes:
                    stop_flag.set()
                cur_n = len(manifest_demos)
                cur_gb = total_bytes / (1024 ** 3)
            pbar.set_postfix(saved=cur_n, gb=f"{cur_gb:.2f}")
            if cur_n % 15 == 0:
                save_manifest(manifest_path, list(manifest_demos.values()))
    pbar.close()

    etf_done = 0
    etf_seen_urls = {e.get("url") for e in manifest_demos.values()}
    etf_queue = [
        d for d in etf2l_direct
        if d["url"] not in etf_seen_urls and int(d["time"]) > 0
        and not any(
            abs(int(d["time"]) - tt) < LEAK_WINDOW_SECONDS and any(normalize_map_family(m) == normalize_map_family(tm) for m in d["maps"])
            for tm, tt in test_keys
        )
    ]
    etf_queue.sort(key=lambda d: (not d["is_pov"], -int(d["time"])))
    etf_pbar = tqdm(total=min(len(etf_queue), args.etf2l_max), desc="ETF2L direct")
    known_sha = {e["sha256"] for e in manifest_demos.values()}
    for d in etf_queue:
        if etf_done >= args.etf2l_max or total_bytes >= max_bytes:
            break
        time.sleep(1.5)
        res = download_etf2l_demo(session, d, demos_dir)
        if res is None:
            continue
        dem_path, map_name = res
        fam = normalize_map_family(map_name)
        sha = file_sha256(dem_path)
        bp = resolve_bsp(session, map_name, maps_dir, neg_cache, allow_download=True)
        if fam in UNSEEN_TEST_FAMILIES or sha in known_sha or bp is None or fam not in TARGET_MAP_PREFIXES:
            dem_path.unlink(missing_ok=True)
            continue
        known_sha.add(sha)
        kind = "pov" if d["is_pov"] else "stv"
        eid = dem_path.stem.split(f"_{kind}_")[0]
        manifest_demos[eid] = {
            "id": eid,
            "source": "etf2l_direct",
            "league": "ETF2L",
            "tier": d["tier"],
            "format": d["format"],
            "map": map_name,
            "map_family": fam,
            "duration": 0,
            "player_count": 0,
            "is_pov": d["is_pov"],
            "nick": "ETF2L POV" if d["is_pov"] else "SourceTV Demo",
            "server": "ETF2L",
            "time": int(d["time"]),
            "url": d["url"],
            "local_path": str(dem_path),
            "bsp_path": str(bp),
            "size_bytes": dem_path.stat().st_size,
            "sha256": sha,
        }
        total_bytes += dem_path.stat().st_size
        etf_done += 1
        etf_pbar.update(1)
        save_manifest(manifest_path, list(manifest_demos.values()))
    etf_pbar.close()

    final_list = list(manifest_demos.values())
    save_manifest(manifest_path, final_list)
    print(f"Manifest saved to {manifest_path}: {len(final_list)} demos, {total_bytes / (1024**3):.2f} GB")


def save_manifest(path: Path, demos: list[dict]) -> None:
    stv_n = sum(1 for d in demos if not d["is_pov"])
    pov_n = sum(1 for d in demos if d["is_pov"])
    maps_n = len(set(d["map"] for d in demos))
    payload = {
        "summary": {
            "total_demos": len(demos),
            "stv_demos": stv_n,
            "pov_demos": pov_n,
            "distinct_maps": maps_n,
            "total_bytes": sum(d["size_bytes"] for d in demos),
            "by_league": dict(Counter(d["league"] for d in demos)),
            "by_map_family": dict(Counter(d["map_family"] for d in demos)),
        },
        "demos": demos,
    }
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    tmp.replace(path)


if __name__ == "__main__":
    main()
