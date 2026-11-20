import argparse
import bz2
import concurrent.futures
import gzip
import hashlib
import json
import os
import re
import time
import zipfile
from collections import Counter
from pathlib import Path
from typing import Any

import requests
from tqdm import tqdm

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
RGL_HIGH_DIVISIONS = ("invite", "advanced", "main", "challenger", "div-1", "div 1")
ETF2L_HIGH_KEYWORDS = ("premiership", "top division", "top tiers", "division 1", "division 2", "high")


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


def collect_etf2l(session: requests.Session, max_comps: int = 12, max_matches_per_comp: int = 12) -> tuple[list[dict], dict[str, dict]]:
    players: dict[str, dict] = {}
    direct_demos: list[dict] = []
    comp_ids: list[tuple[int, str, str]] = []
    for page in range(1, 5):
        data = fetch_json(session, f"https://api-v2.etf2l.org/competition/list?limit=50&page={page}")
        if not data:
            break
        comps = data.get("competitions", {}).get("data", [])
        if not comps:
            break
        for c in comps:
            ctype = c.get("type", "")
            cname = c.get("name", "")
            cat = c.get("category", "")
            if ctype not in ("6v6", "Highlander"):
                continue
            if "Season" not in cat and "Playoffs" not in cname:
                continue
            nl = cname.lower()
            if any(k in nl for k in ETF2L_HIGH_KEYWORDS):
                comp_ids.append((int(c["id"]), cname, ctype))
        if len(comp_ids) >= max_comps:
            break

    for cid, cname, ctype in comp_ids[:max_comps]:
        res = fetch_json(session, f"https://api-v2.etf2l.org/competition/{cid}/results?limit=25")
        if not res:
            continue
        matches = [m for m in res.get("results", {}).get("data", []) if not m.get("defaultwin") and m.get("id")]
        mids = [m["id"] for m in matches[:max_matches_per_comp]]

        def fetch_match(mid: int):
            return mid, fetch_json(make_session(), f"https://api-v2.etf2l.org/matches/{mid}")

        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
            for mid, md_wrap in ex.map(fetch_match, mids):
                if not md_wrap or "match" not in md_wrap:
                    continue
                md = md_wrap["match"]
                div_name = (md.get("division") or {}).get("name", "Unknown")
                div_tier = (md.get("division") or {}).get("tier")
                m_players = md.get("players") or []
                if not m_players:
                    continue
                for p in m_players:
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
        teams = (s.get("participatingTeams") or [])[:max_teams_per_season * 2]

        def fetch_team(tid: int):
            return fetch_json(make_session(), f"https://api.rgl.gg/v0/teams/{tid}", timeout=8)

        checked = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:
            for t in ex.map(fetch_team, teams):
                if checked >= max_teams_per_season:
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


def collect_ozfortress_players(session: requests.Session, league_ids: list[int], max_users: int = 24) -> dict[str, dict]:
    players: dict[str, dict] = {}
    user_ids: list[str] = []
    for lid in league_ids:
        html = fetch_text(session, f"https://ozfortress.com/leagues/{lid}")
        if not html:
            continue
        team_paths = sorted(set(re.findall(r"/teams/\d+", html)))
        for tp in team_paths[:10]:
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

    for up in user_ids[:max_users]:
        uh = fetch_text(session, f"https://ozfortress.com{up}")
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


def collect_logstf_index(session: requests.Session, pages: int = 3) -> dict[tuple[str, int], dict]:
    index: dict[tuple[str, int], dict] = {}
    for p in range(pages):
        data = fetch_json(session, f"https://logs.tf/api/v1/log?limit=100&offset={p * 100}")
        if not data or not data.get("logs"):
            break
        for row in data["logs"]:
            m = (row.get("map") or "").lower()
            dt = int(row.get("date") or 0)
            if m and dt:
                index[(m, dt // 600)] = row
    return index


def make_demostf_record(d: dict, source: str, league: str, tier: str, fmt: str) -> dict | None:
    url = d.get("url")
    if not url:
        return None
    dur = int(d.get("duration") or 0)
    pc = int(d.get("playerCount") or 0)
    if dur < 600 or dur > 7200 or pc < 12:
        return None
    m = (d.get("map") or "").strip().lower()
    if not m:
        return None
    nick = (d.get("nick") or "").strip()
    is_pov = nick != "SourceTV Demo"
    if fmt == "unknown":
        fmt = "Highlander" if pc >= 18 else "6v6"
    return {
        "id": f"demostf_{d['id']}",
        "demostf_id": int(d["id"]),
        "source": source,
        "league": league,
        "tier": tier,
        "format": fmt,
        "map": m,
        "map_family": normalize_map_family(m),
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
    pages_per_map: int = 4,
    pov_scan_pages: int = 12,
) -> dict[str, dict]:
    candidates: dict[str, dict] = {}
    urls = [
        ("map", f"https://api.demos.tf/demos?map={mp}&page={page}")
        for mp in map_prefixes
        for page in range(1, pages_per_map + 1)
    ] + [
        ("pov", f"https://api.demos.tf/demos?page={page}")
        for page in range(1, pov_scan_pages + 1)
    ]

    def fetch_page(item: tuple[str, str]):
        kind, url = item
        return kind, fetch_json(make_session(), url)

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
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
    max_per_map_stv: int = 10,
    max_per_map_pov: int = 6,
) -> list[dict]:
    def score(r: dict) -> tuple:
        league_prio = 0 if r["league"] in ("ETF2L", "RGL", "ozfortress") else 1
        tier_l = r["tier"].lower()
        tier_prio = 0 if ("prem" in tier_l or "invite" in tier_l) else (1 if "div" in tier_l or "adv" in tier_l or "high" in tier_l else 2)
        fam_prio = 0 if r["map_family"] in TARGET_MAP_PREFIXES[:13] else 1
        return (league_prio, tier_prio, fam_prio, -r["time"])

    povs = sorted([c for c in candidates if c["is_pov"]], key=score)
    stvs = sorted([c for c in candidates if not c["is_pov"]], key=score)

    selected: list[dict] = []
    selected_ids: set[str] = set()
    pov_counts: Counter = Counter()
    stv_counts: Counter = Counter()

    def pick_round_robin(pool: list[dict], target: int, counts: Counter, max_cap: int) -> None:
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


def file_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, default=Path("ml_pipeline/data"))
    parser.add_argument("--target-stv", type=int, default=110)
    parser.add_argument("--target-pov", type=int, default=40)
    parser.add_argument("--max-gb", type=float, default=8.5)
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
    session = make_session()

    print("Querying ETF2L competitions and verified high-div rosters...", flush=True)
    etf2l_direct, etf2l_players = collect_etf2l(session)
    print(f"  ETF2L players: {len(etf2l_players)}, direct demo entries: {len(etf2l_direct)}", flush=True)

    print("Querying RGL high-div rosters...", flush=True)
    rgl_players = collect_rgl_players(session, [199, 198, 192, 193], max_teams_per_season=8)
    print(f"  RGL players: {len(rgl_players)}", flush=True)

    print("Querying ozfortress rosters...", flush=True)
    oz_players = collect_ozfortress_players(session, [99, 95])
    print(f"  ozfortress players: {len(oz_players)}", flush=True)

    print("Indexing recent logs.tf matches for cross-verification...", flush=True)
    logs_index = collect_logstf_index(session, pages=3)
    print(f"  logs.tf index buckets: {len(logs_index)}", flush=True)

    all_players = {}
    etf_items = list(etf2l_players.items())[:25]
    rgl_items = list(rgl_players.items())[:25]
    oz_items = list(oz_players.items())[:15]
    for k, v in etf_items + rgl_items + oz_items:
        all_players[k] = v

    print("Querying demos.tf for league-verified players...", flush=True)
    candidates_map = query_demostf_by_players(session, all_players, max_queries=65)
    print(f"  League-matched demos.tf candidates: {len(candidates_map)}", flush=True)

    print("Querying demos.tf across competitive maps and recent POV uploads...")
    map_candidates = query_demostf_by_maps(session, TARGET_MAP_PREFIXES[:14], pages_per_map=4, pov_scan_pages=18)
    for k, v in map_candidates.items():
        if k not in candidates_map:
            candidates_map[k] = v

    for d in etf2l_direct:
        if d["maps"]:
            m0 = d["maps"][0].lower()
            rec = {
                "id": f"etf2l_{d['match_id']}_{'pov' if d['is_pov'] else 'stv'}",
                "source": "etf2l_direct",
                "league": "ETF2L",
                "tier": d["tier"],
                "format": d["format"],
                "map": m0,
                "map_family": normalize_map_family(m0),
                "duration": 1800,
                "player_count": 18 if d["format"] == "Highlander" else 12,
                "is_pov": d["is_pov"],
                "nick": "ETF2L POV" if d["is_pov"] else "SourceTV Demo",
                "server": "ETF2L",
                "time": int(d["time"]),
                "hash": d["url"],
                "url": d["url"],
            }
            if rec["hash"] not in candidates_map:
                candidates_map[rec["hash"]] = rec

    all_candidates = [c for c in candidates_map.values() if c["map_family"] in TARGET_MAP_PREFIXES]
    for c in all_candidates:
        bucket = (c["map"], c["time"] // 600)
        if bucket in logs_index:
            c["logstf_id"] = logs_index[bucket]["id"]

    neg_cache: set[str] = set()
    unique_maps = sorted({c["map"] for c in all_candidates})
    print(f"Total deduplicated candidates: {len(all_candidates)} across {len(unique_maps)} maps. Checking BSP availability...", flush=True)

    def check_map(m: str):
        return m, resolve_bsp(make_session(), m, maps_dir, set(), allow_download=False)

    map_bsp_cache: dict[str, Path | None] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
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
    print(f"BSP-verified candidates: {len(bsp_ok_candidates)} (STV={stv_avail}, POV={pov_avail})")
    print("Map family availability:", dict(fam_counts.most_common()))

    selected = select_balanced_demos(bsp_ok_candidates, args.target_stv, args.target_pov)
    sel_pov = sum(1 for c in selected if c["is_pov"])
    sel_stv = sum(1 for c in selected if not c["is_pov"])
    sel_fams = Counter(c["map_family"] for c in selected)
    sel_leagues = Counter(c["league"] for c in selected)
    print(f"Selected batch: {len(selected)} demos (STV={sel_stv}, POV={sel_pov})")
    print("Selected map families:", dict(sel_fams.most_common()))
    print("Selected leagues:", dict(sel_leagues.most_common()))

    if args.dry_run:
        return

    max_bytes = int(args.max_gb * 1024 * 1024 * 1024)
    total_bytes = sum(Path(e["local_path"]).stat().st_size for e in existing_by_id.values())
    manifest_demos: dict[str, dict] = dict(existing_by_id)

    pbar = tqdm(selected, desc="Downloading demos")
    for rec in pbar:
        if rec["id"] in manifest_demos:
            continue
        if total_bytes >= max_bytes:
            print(f"Reached disk budget {args.max_gb:.2f} GB, stopping downloads.")
            break
        bsp_path = resolve_bsp(session, rec["map"], maps_dir, neg_cache, allow_download=True)
        if bsp_path is None:
            continue
        dem_path = download_demo_file(session, rec, demos_dir)
        if dem_path is None:
            continue
        sz = dem_path.stat().st_size
        total_bytes += sz
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
            "bsp_path": str(bsp_path),
            "size_bytes": sz,
            "sha256": file_sha256(dem_path),
        }
        if "logstf_id" in rec:
            entry["logstf_id"] = rec["logstf_id"]
        manifest_demos[rec["id"]] = entry
        pbar.set_postfix(saved=len(manifest_demos), gb=f"{total_bytes / (1024**3):.2f}")

        if len(manifest_demos) % 10 == 0:
            save_manifest(manifest_path, list(manifest_demos.values()))

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
