mod bsp;
mod physics;

use bitbuffer::BitRead;
use bsp::BspMap;
use clap::{Parser, Subcommand};
use physics::{class_default_maxspeed, infer_wish_dir};
use rayon::prelude::*;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, HashMap};
use std::fs::{self, File};
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};
use tf_demo_parser::demo::data::userinfo::UserInfo;
use tf_demo_parser::demo::header::Header;
use tf_demo_parser::demo::message::Message;
use tf_demo_parser::demo::packet::datatable::ServerClassName;
use tf_demo_parser::demo::packet::Packet;
use tf_demo_parser::demo::parser::RawPacketStream;
use tf_demo_parser::demo::sendprop::SendPropValue;
use tf_demo_parser::{MessageType, ParserState, Stream};

const INVALID_EHANDLE: i64 = 2097151;
const ENT_INDEX_MASK: i64 = 0x7FF;
const FL_ONGROUND: u32 = 1 << 0;
const FL_DUCKING: u32 = 1 << 1;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct FrameRecord {
    pub steam_id: u64,
    pub demo_tick: i32,
    pub server_tick: i32,
    pub origin: [f32; 3],
    pub velocity: [f32; 3],
    pub eye_angles: [f32; 2],
    pub wish_fwd: f32,
    pub wish_side: f32,
    pub maxspeed: f32,
    pub sim_time: f32,
    pub ground_dist: f32,
    pub ground_normal: [f32; 3],
    pub ceiling_dist: f32,
    pub wall_dists: [f32; 8],
    pub ledge_depths: [f32; 8],
    pub player_cond: [u32; 5],
    pub flags: u32,
    pub buttons: u32,
    pub active_weapon: i32,
    pub entity_id: u16,
    pub health: i16,
    pub max_health: i16,
    pub team: u8,
    pub class_id: u8,
    pub water_level: u8,
    pub air_dash: u8,
    pub air_ducked: u8,
    pub jumping: u8,
    pub vel_source: u8,
    pub input_source: u8,
    pub wall_contact: u8,
    pub pad: [u8; 5],
}

const _: () = assert!(std::mem::size_of::<FrameRecord>() == 200);

#[derive(Clone, Default)]
struct PlayerTrackState {
    entity_id: u16,
    steam_id: u64,
    team: u8,
    class_id: u8,
    health: i16,
    max_health: i16,
    life_state: i64,
    origin: [f32; 3],
    velocity: [f32; 3],
    eye_angles: [f32; 2],
    flags: u32,
    maxspeed: f32,
    sim_tick: i64,
    water_level: u8,
    player_cond: [u32; 5],
    air_dash: u8,
    air_ducked: u8,
    jumping: u8,
    active_weapon_handle: i64,
    in_pvs: bool,
    has_live_vel: bool,
    vel_updates: u32,
    prev_origin: [f32; 3],
    prev_sim_tick: i64,
    fd_vel: [f32; 3],
    has_prev: bool,
}

#[derive(Clone)]
struct RawFrame {
    demo_tick: i32,
    server_tick: i32,
    entity_id: u16,
    steam_id: u64,
    team: u8,
    class_id: u8,
    health: i16,
    max_health: i16,
    origin: [f32; 3],
    velocity: [f32; 3],
    eye_angles: [f32; 2],
    flags: u32,
    maxspeed: f32,
    sim_time: f32,
    water_level: u8,
    player_cond: [u32; 5],
    air_dash: u8,
    air_ducked: u8,
    jumping: u8,
    active_weapon: i32,
    vel_source: u8,
    sim_tick_raw: i64,
}

#[derive(Clone, Copy)]
struct UserCmdSample {
    demo_tick: i32,
    tick_count: i32,
    pitch: f32,
    yaw: f32,
    fwd: f32,
    side: f32,
    buttons: u32,
}

#[derive(Serialize, Deserialize)]
struct ExtractMeta {
    demo_path: String,
    bsp_path: String,
    map: String,
    nick: String,
    is_stv: bool,
    local_entity_id: u16,
    interval_per_tick: f32,
    total_frames: usize,
    usercmd_packets: usize,
    aligned_usercmd_frames: usize,
    usercmd_lag_ticks: i32,
    inv_phys_eval_frames: usize,
    inv_phys_mean_cos_sim: f32,
    inv_phys_mean_ang_err_deg: f32,
    inv_phys_dir_agree_45deg: f32,
    inv_phys_ground_eval_frames: usize,
    inv_phys_ground_cos_sim: f32,
    inv_phys_ground_agree_45deg: f32,
    inv_phys_idle_agree: f32,
    ground_frames: usize,
    ground_dist_median: f32,
    ground_dist_p95: f32,
    ground_nz_ge_07_frac: f32,
    record_size_bytes: usize,
}

#[derive(Deserialize)]
struct ManifestFile {
    demos: Vec<ManifestEntry>,
}

#[derive(Deserialize)]
struct ManifestEntry {
    id: String,
    local_path: String,
    bsp_path: String,
}

#[derive(Parser)]
struct Cli {
    #[command(subcommand)]
    cmd: Commands,
}

#[derive(Subcommand)]
enum Commands {
    Extract {
        #[arg(long)]
        demo: PathBuf,
        #[arg(long)]
        bsp: PathBuf,
        #[arg(long)]
        out: PathBuf,
    },
    Batch {
        #[arg(long)]
        manifest: PathBuf,
        #[arg(long)]
        out_dir: PathBuf,
        #[arg(long, default_value_t = 4)]
        jobs: usize,
    },
}

fn parse_steam_id64(s: &str) -> u64 {
    let trimmed = s.trim();
    if trimmed.starts_with("[U:1:") && trimmed.ends_with(']') {
        if let Ok(acc) = trimmed[5..trimmed.len() - 1].parse::<u64>() {
            return 76561197960265728u64 + acc;
        }
    } else if let Some(rest) = trimmed.strip_prefix("STEAM_") {
        let parts: Vec<&str> = rest.split(':').collect();
        if parts.len() == 3 {
            if let (Ok(y), Ok(z)) = (parts[1].parse::<u64>(), parts[2].parse::<u64>()) {
                return 76561197960265728u64 + z * 2 + y;
            }
        }
    } else if let Ok(v) = trimmed.parse::<u64>() {
        if v > 76561190000000000u64 {
            return v;
        }
    }
    0
}

fn find_next_sim_frame(frames: &[RawFrame], i: usize) -> Option<(usize, u32)> {
    let f = &frames[i];
    for j in (i + 1)..frames.len().min(i + 8) {
        let fj = &frames[j];
        let d_sim = (fj.sim_tick_raw - f.sim_tick_raw).rem_euclid(100);
        if d_sim > 0 && d_sim <= 6 {
            let dx = fj.origin[0] - f.origin[0];
            let dy = fj.origin[1] - f.origin[1];
            let dz = fj.origin[2] - f.origin[2];
            if dx * dx + dy * dy + dz * dz <= 400.0 * 400.0 {
                return Some((j, d_sim as u32));
            }
            return None;
        }
    }
    None
}
#[inline]
fn angle_diff_deg(a: f32, b: f32) -> f32 {
    let mut d = (a - b) % 360.0;
    if d > 180.0 {
        d -= 360.0;
    }
    if d < -180.0 {
        d += 360.0;
    }
    d
}

fn extract_one(demo_path: &Path, bsp_path: &Path, out_bin_path: &Path) -> Result<ExtractMeta, String> {
    let bsp = BspMap::load(bsp_path)?;
    let bytes = fs::read(demo_path).map_err(|e| format!("read demo: {}", e))?;
    let mut stream = Stream::from(bytes.as_slice());
    let header = Header::read(&mut stream).map_err(|e| format!("read header: {:?}", e))?;

    let mut state = ParserState::new(
        header.protocol,
        |mt| matches!(mt, MessageType::PacketEntities | MessageType::ServerInfo | MessageType::NetTick),
        false,
    );
    let mut packets = RawPacketStream::new(stream);
    let mut class_names: Vec<ServerClassName> = Vec::new();
    let mut string_table_names: Vec<String> = Vec::new();

    let mut is_stv = header.nick == "SourceTV Demo";
    let mut local_eid: u16 = 0;
    let mut interval_per_tick = 0.015f32;
    let mut server_tick: i32 = 0;

    let mut players: HashMap<u16, PlayerTrackState> = HashMap::new();
    let mut weapons_def: HashMap<u16, i32> = HashMap::new();
    let mut weapons_class: HashMap<u16, i32> = HashMap::new();
    let mut usercmds: Vec<UserCmdSample> = Vec::new();
    let mut raw_frames_by_player: BTreeMap<u16, Vec<RawFrame>> = BTreeMap::new();

    while let Some(pkt) = packets.next(&state).map_err(|e| format!("packet err: {:?}", e))? {
        match pkt {
            Packet::DataTables(dt) => {
                class_names = dt.server_classes.iter().map(|c| c.name.clone()).collect();
                state
                    .handle_data_table(&dt.tables, dt.server_classes)
                    .map_err(|e| format!("dt err: {:?}", e))?;
            }
            Packet::StringTables(st) => {
                for t in st.tables {
                    state.handle_string_table_meta(t.get_table_meta());
                    let is_ui = t.name == "userinfo";
                    for (idx, entry) in t.entries {
                        if is_ui {
                            if let Ok(Some(ui)) = UserInfo::parse_from_string_table(
                                idx,
                                entry.text.as_deref(),
                                entry.extra_data.as_ref().map(|e| e.data.clone()),
                            ) {
                                let eid = u32::from(ui.entity_id) as u16;
                                let p = players.entry(eid).or_insert_with(|| {
                                    let mut st = PlayerTrackState::default();
                                    st.entity_id = eid;
                                    st
                                });
                                p.steam_id = parse_steam_id64(&ui.player_info.steam_id);
                            }
                        }
                        state.handle_string_entry(&t.name, idx as usize, &entry);
                    }
                    string_table_names.push(t.name.into_owned());
                }
            }
            Packet::UserCmd(uc) => {
                let tick_cnt = uc.cmd.tick_count.unwrap_or(0) as i32;
                let pitch = uc.cmd.view_angles[0].unwrap_or(0.0);
                let yaw = uc.cmd.view_angles[1].unwrap_or(0.0);
                let fwd = uc.cmd.movement[0].unwrap_or(0.0);
                let side = uc.cmd.movement[1].unwrap_or(0.0);
                let buttons = uc.cmd.buttons.unwrap_or(0);
                usercmds.push(UserCmdSample {
                    demo_tick: u32::from(uc.tick) as i32,
                    tick_count: tick_cnt,
                    pitch,
                    yaw,
                    fwd,
                    side,
                    buttons,
                });
            }
            Packet::Message(mp) | Packet::Signon(mp) => {
                let demo_tick = u32::from(mp.tick) as i32;
                let mut had_entities = false;
                for msg in mp.messages {
                    match msg {
                        Message::ServerInfo(si) => {
                            is_stv = si.stv;
                            local_eid = (si.player_slot as u16) + 1;
                            if si.interval_per_tick > 0.0 {
                                interval_per_tick = si.interval_per_tick;
                            }
                        }
                        Message::NetTick(nt) => {
                            server_tick = u32::from(nt.tick) as i32;
                            state.handle_message(Message::NetTick(nt), mp.tick);
                        }
                        Message::CreateStringTable(cst) => {
                            state.handle_string_table_meta(cst.table.get_table_meta());
                            let is_ui = cst.table.name == "userinfo";
                            for (idx, entry) in cst.table.entries {
                                if is_ui {
                                    if let Ok(Some(ui)) = UserInfo::parse_from_string_table(
                                        idx,
                                        entry.text.as_deref(),
                                        entry.extra_data.as_ref().map(|e| e.data.clone()),
                                    ) {
                                        let eid = u32::from(ui.entity_id) as u16;
                                        let p = players.entry(eid).or_insert_with(|| {
                                            let mut st = PlayerTrackState::default();
                                            st.entity_id = eid;
                                            st
                                        });
                                        p.steam_id = parse_steam_id64(&ui.player_info.steam_id);
                                    }
                                }
                                state.handle_string_entry(&cst.table.name, idx as usize, &entry);
                            }
                            string_table_names.push(cst.table.name.into_owned());
                        }
                        Message::UpdateStringTable(ust) => {
                            if let Some(name) = string_table_names.get(ust.table_id as usize) {
                                let is_ui = name == "userinfo";
                                for (idx, entry) in ust.entries {
                                    if is_ui {
                                        if let Ok(Some(ui)) = UserInfo::parse_from_string_table(
                                            idx,
                                            entry.text.as_deref(),
                                            entry.extra_data.as_ref().map(|e| e.data.clone()),
                                        ) {
                                            let eid = u32::from(ui.entity_id) as u16;
                                            let p = players.entry(eid).or_insert_with(|| {
                                                let mut st = PlayerTrackState::default();
                                                st.entity_id = eid;
                                                st
                                            });
                                            p.steam_id = parse_steam_id64(&ui.player_info.steam_id);
                                        }
                                    }
                                    state.handle_string_entry(name, idx as usize, &entry);
                                }
                            }
                        }
                        Message::PacketEntities(pe) => {
                            had_entities = true;
                            for rem in &pe.removed_entities {
                                let eid = u32::from(*rem) as u16;
                                weapons_def.remove(&eid);
                                weapons_class.remove(&eid);
                            }
                            for ent in &pe.entities {
                                let sc_idx = usize::from(ent.server_class);
                                let Some(cn) = class_names.get(sc_idx) else {
                                    continue;
                                };
                                let cname = cn.as_str();
                                if cname == "CTFPlayer" {
                                    let eid = u32::from(ent.entity_index) as u16;
                                    let p = players.entry(eid).or_insert_with(|| {
                                        let mut st = PlayerTrackState::default();
                                        st.entity_id = eid;
                                        st
                                    });
                                    p.in_pvs = ent.in_pvs;
                                    for prop in ent.props(&state) {
                                        if let Some((_, pname)) = prop.identifier.names() {
                                            match pname.as_str() {
                                                "m_iHealth" => {
                                                    p.health = i64::try_from(&prop.value).unwrap_or(0) as i16;
                                                }
                                                "m_iMaxHealth" => {
                                                    p.max_health = i64::try_from(&prop.value).unwrap_or(0) as i16;
                                                }
                                                "m_lifeState" => {
                                                    p.life_state = i64::try_from(&prop.value).unwrap_or(0);
                                                }
                                                "m_vecOrigin" => {
                                                    if let SendPropValue::VectorXY(v) = prop.value {
                                                        p.origin[0] = v.x;
                                                        p.origin[1] = v.y;
                                                    }
                                                }
                                                "m_vecOrigin[2]" => {
                                                    if let Ok(v) = f32::try_from(&prop.value) {
                                                        p.origin[2] = v;
                                                    }
                                                }
                                                "m_vecVelocity[0]" => {
                                                    if let Ok(v) = f32::try_from(&prop.value) {
                                                        p.velocity[0] = v;
                                                        if v.abs() > 1e-3 {
                                                            p.vel_updates += 1;
                                                            p.has_live_vel = true;
                                                        }
                                                    }
                                                }
                                                "m_vecVelocity[1]" => {
                                                    if let Ok(v) = f32::try_from(&prop.value) {
                                                        p.velocity[1] = v;
                                                        if v.abs() > 1e-3 {
                                                            p.vel_updates += 1;
                                                            p.has_live_vel = true;
                                                        }
                                                    }
                                                }
                                                "m_vecVelocity[2]" => {
                                                    if let Ok(v) = f32::try_from(&prop.value) {
                                                        p.velocity[2] = v;
                                                        if v.abs() > 1e-3 {
                                                            p.vel_updates += 1;
                                                            p.has_live_vel = true;
                                                        }
                                                    }
                                                }
                                                "m_angEyeAngles[0]" => {
                                                    if let Ok(v) = f32::try_from(&prop.value) {
                                                        p.eye_angles[0] = v;
                                                    }
                                                }
                                                "m_angEyeAngles[1]" => {
                                                    if let Ok(v) = f32::try_from(&prop.value) {
                                                        p.eye_angles[1] = v;
                                                    }
                                                }
                                                "m_fFlags" => {
                                                    p.flags = i64::try_from(&prop.value).unwrap_or(0) as u32;
                                                }
                                                "m_flMaxspeed" => {
                                                    if let Ok(v) = f32::try_from(&prop.value) {
                                                        p.maxspeed = v;
                                                    }
                                                }
                                                "m_flSimulationTime" => {
                                                    p.sim_tick = i64::try_from(&prop.value).unwrap_or(0);
                                                }
                                                "m_nWaterLevel" => {
                                                    p.water_level = i64::try_from(&prop.value).unwrap_or(0) as u8;
                                                }
                                                "m_nPlayerCond" | "_condition_bits" => {
                                                    p.player_cond[0] = i64::try_from(&prop.value).unwrap_or(0) as u32;
                                                }
                                                "m_nPlayerCondEx" => {
                                                    p.player_cond[1] = i64::try_from(&prop.value).unwrap_or(0) as u32;
                                                }
                                                "m_nPlayerCondEx2" => {
                                                    p.player_cond[2] = i64::try_from(&prop.value).unwrap_or(0) as u32;
                                                }
                                                "m_nPlayerCondEx3" => {
                                                    p.player_cond[3] = i64::try_from(&prop.value).unwrap_or(0) as u32;
                                                }
                                                "m_nPlayerCondEx4" => {
                                                    p.player_cond[4] = i64::try_from(&prop.value).unwrap_or(0) as u32;
                                                }
                                                "m_iAirDash" => {
                                                    p.air_dash = i64::try_from(&prop.value).unwrap_or(0) as u8;
                                                }
                                                "m_nAirDucked" => {
                                                    p.air_ducked = i64::try_from(&prop.value).unwrap_or(0) as u8;
                                                }
                                                "m_bJumping" => {
                                                    p.jumping = i64::try_from(&prop.value).unwrap_or(0) as u8;
                                                }
                                                "m_hActiveWeapon" => {
                                                    p.active_weapon_handle = i64::try_from(&prop.value).unwrap_or(INVALID_EHANDLE);
                                                }
                                                _ => {}
                                            }
                                        }
                                    }
                                } else if cname == "CTFPlayerResource" {
                                    for prop in ent.props(&state) {
                                        if let Some((tname, pname)) = prop.identifier.names() {
                                            if let Ok(pid) = pname.as_str().parse::<u16>() {
                                                let p = players.entry(pid).or_insert_with(|| {
                                                    let mut st = PlayerTrackState::default();
                                                    st.entity_id = pid;
                                                    st
                                                });
                                                match tname.as_str() {
                                                    "m_iTeam" => {
                                                        p.team = i64::try_from(&prop.value).unwrap_or(0) as u8;
                                                    }
                                                    "m_iPlayerClass" => {
                                                        p.class_id = i64::try_from(&prop.value).unwrap_or(0) as u8;
                                                    }
                                                    "m_iMaxHealth" => {
                                                        p.max_health = i64::try_from(&prop.value).unwrap_or(0) as i16;
                                                    }
                                                    _ => {}
                                                }
                                            }
                                        }
                                    }
                                } else if cname.starts_with("CTF") || cname.starts_with("CWeapon") || cname == "CTFWearableDemoShield" {
                                    let eid = u32::from(ent.entity_index) as u16;
                                    weapons_class.insert(eid, -(sc_idx as i32));
                                    for prop in ent.props(&state) {
                                        if let Some((_, pname)) = prop.identifier.names() {
                                            if pname.as_str() == "m_iItemDefinitionIndex" {
                                                if let Ok(def) = i64::try_from(&prop.value) {
                                                    weapons_def.insert(eid, def as i32);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            state.handle_message(Message::PacketEntities(pe), mp.tick);
                        }
                        other => state.handle_message(other, mp.tick),
                    }
                }

                if had_entities && demo_tick > 0 {
                    for (&eid, p) in &mut players {
                        if p.life_state != 0 || p.health <= 0 || (p.team != 2 && p.team != 3) || p.class_id == 0 || !p.in_pvs {
                            p.has_prev = false;
                            continue;
                        }
                        if p.origin[0].abs() < 1e-3 && p.origin[1].abs() < 1e-3 && p.origin[2].abs() < 1e-3 {
                            continue;
                        }

                        let use_live_vel = !is_stv && eid == local_eid || (is_stv && p.has_live_vel);
                        let (vel, vel_source) = if use_live_vel {
                            (p.velocity, 1u8)
                        } else if p.has_prev {
                            let dx = p.origin[0] - p.prev_origin[0];
                            let dy = p.origin[1] - p.prev_origin[1];
                            let dz = p.origin[2] - p.prev_origin[2];
                            let dist2 = dx * dx + dy * dy + dz * dz;
                            let sim_dt_ticks = (p.sim_tick - p.prev_sim_tick).rem_euclid(100);
                            if dist2 > 200.0 * 200.0 {
                                ([0.0, 0.0, 0.0], 0u8)
                            } else if sim_dt_ticks > 0 && sim_dt_ticks <= 8 {
                                let dt = (sim_dt_ticks as f32) * interval_per_tick;
                                let raw_vx = dx / dt;
                                let raw_vy = dy / dt;
                                let raw_vz = dz / dt;
                                p.fd_vel[0] = 0.65 * raw_vx + 0.35 * p.fd_vel[0];
                                p.fd_vel[1] = 0.65 * raw_vy + 0.35 * p.fd_vel[1];
                                p.fd_vel[2] = 0.65 * raw_vz + 0.35 * p.fd_vel[2];
                                (p.fd_vel, 0u8)
                            } else {
                                (p.fd_vel, 0u8)
                            }
                        } else {
                            p.fd_vel = [0.0, 0.0, 0.0];
                            ([0.0, 0.0, 0.0], 0u8)
                        };

                        p.prev_origin = p.origin;
                        p.prev_sim_tick = p.sim_tick;
                        p.has_prev = true;

                        let active_weapon = if p.active_weapon_handle != INVALID_EHANDLE && p.active_weapon_handle > 0 {
                            let weid = (p.active_weapon_handle & ENT_INDEX_MASK) as u16;
                            weapons_def.get(&weid).copied().or_else(|| weapons_class.get(&weid).copied()).unwrap_or(-1)
                        } else {
                            -1
                        };

                        let eff_maxspeed = if p.maxspeed > 10.0 {
                            p.maxspeed
                        } else {
                            class_default_maxspeed(p.class_id)
                        };

                        raw_frames_by_player.entry(eid).or_default().push(RawFrame {
                            demo_tick,
                            server_tick,
                            entity_id: eid,
                            steam_id: p.steam_id,
                            team: p.team,
                            class_id: p.class_id,
                            health: p.health,
                            max_health: p.max_health,
                            origin: p.origin,
                            velocity: vel,
                            eye_angles: p.eye_angles,
                            flags: p.flags,
                            maxspeed: eff_maxspeed,
                            sim_time: (p.sim_tick as f32) * interval_per_tick,
                            water_level: p.water_level,
                            player_cond: p.player_cond,
                            air_dash: p.air_dash,
                            air_ducked: p.air_ducked,
                            jumping: p.jumping,
                            active_weapon,
                            vel_source,
                            sim_tick_raw: p.sim_tick,
                        });
                    }
                }
            }
            _ => {}
        }
    }

    let mut uc_by_tick_count: HashMap<i32, UserCmdSample> = HashMap::with_capacity(usercmds.len());
    for uc in &usercmds {
        uc_by_tick_count.insert(uc.tick_count, *uc);
    }

    let mut best_lag: i32 = 0;
    if !is_stv && !usercmds.is_empty() {
        if let Some(local_frames) = raw_frames_by_player.get(&local_eid) {
            let mut best_score = f64::NEG_INFINITY;
            for lag in -12i32..=12i32 {
                let mut score = 0.0f64;
                let mut count = 0usize;
                for i in 0..local_frames.len().saturating_sub(1) {
                    let f0 = &local_frames[i];
                    let Some((j, d_sim)) = find_next_sim_frame(local_frames, i) else {
                        continue;
                    };
                    let f1 = &local_frames[j];
                    if let Some(uc) = uc_by_tick_count.get(&(f0.server_tick - lag)) {
                        let yaw_err = angle_diff_deg(uc.yaw, f0.eye_angles[1]).abs();
                        score += 1.0 - (yaw_err / 180.0) as f64;
                        if uc.fwd.abs() > 10.0 || uc.side.abs() > 10.0 {
                            let on_ground = (f0.flags & FL_ONGROUND) != 0;
                            let ducking = (f0.flags & FL_DUCKING) != 0;
                            let lost_footing = (f0.player_cond[3] & (1 << (126 - 96))) != 0;
                            let (inv_f, inv_s) = infer_wish_dir(
                                [f0.velocity[0], f0.velocity[1]],
                                [f1.velocity[0], f1.velocity[1]],
                                f0.eye_angles[1],
                                on_ground,
                                ducking,
                                f0.maxspeed,
                                f0.class_id,
                                d_sim,
                                lost_footing,
                            );
                            let umag = (uc.fwd * uc.fwd + uc.side * uc.side).sqrt();
                            let imag = (inv_f * inv_f + inv_s * inv_s).sqrt();
                            if umag > 1e-3 && imag > 1e-3 {
                                let cos = ((uc.fwd / umag) * (inv_f / imag) + (uc.side / umag) * (inv_s / imag)) as f64;
                                score += 2.0 * cos;
                            }
                        }
                        count += 1;
                    }
                    if count >= 4000 {
                        break;
                    }
                }
                if count > 0 && score > best_score {
                    best_score = score;
                    best_lag = lag;
                }
            }
        }
    }

    let mut total_frames = 0usize;
    let mut aligned_usercmd_frames = 0usize;
    let mut inv_eval_frames = 0usize;
    let mut inv_cos_sum = 0.0f64;
    let mut inv_ang_sum = 0.0f64;
    let mut inv_agree_45 = 0usize;
    let mut inv_g_eval_frames = 0usize;
    let mut inv_g_cos_sum = 0.0f64;
    let mut inv_g_agree_45 = 0usize;
    let mut idle_total = 0usize;
    let mut idle_agree = 0usize;
    let mut ground_dists: Vec<f32> = Vec::new();
    let mut ground_nz_ok = 0usize;

    if let Some(parent) = out_bin_path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    let out_file = File::create(out_bin_path).map_err(|e| e.to_string())?;
    let mut writer = BufWriter::with_capacity(1 << 20, out_file);

    for (&eid, frames) in &raw_frames_by_player {
        for i in 0..frames.len() {
            let f = &frames[i];
            let on_ground = (f.flags & FL_ONGROUND) != 0;
            let ducking = (f.flags & FL_DUCKING) != 0;
            let lost_footing = (f.player_cond[3] & (1 << (126 - 96))) != 0;

            let next_sim = find_next_sim_frame(frames, i);
            let (inv_fwd, inv_side, has_dv) = if let Some((j, d_sim)) = next_sim {
                let f_next = &frames[j];
                let dvx = f_next.velocity[0] - f.velocity[0];
                let dvy = f_next.velocity[1] - f.velocity[1];
                let dv_sq = dvx * dvx + dvy * dvy;
                let (iwf, iws) = infer_wish_dir(
                    [f.velocity[0], f.velocity[1]],
                    [f_next.velocity[0], f_next.velocity[1]],
                    f.eye_angles[1],
                    on_ground,
                    ducking,
                    f.maxspeed,
                    f.class_id,
                    d_sim,
                    lost_footing,
                );
                (iwf, iws, dv_sq > 1.0)
            } else {
                (0.0, 0.0, false)
            };

            let geo = bsp.probe_geometry(f.origin, f.eye_angles[1], ducking);
            if on_ground {
                ground_dists.push(geo.ground_dist);
                if geo.ground_normal[2] >= 0.7 && geo.ground_dist <= 4.0 {
                    ground_nz_ok += 1;
                }
            }

            let mut wish_fwd = inv_fwd;
            let mut wish_side = inv_side;
            let mut input_source = 0u8;
            let mut buttons = 0u32;

            if !is_stv && eid == local_eid {
                if let Some(uc) = uc_by_tick_count.get(&(f.server_tick - best_lag)) {
                    let umag = (uc.fwd * uc.fwd + uc.side * uc.side).sqrt();
                    if umag > 1e-3 {
                        let scale = umag.max(450.0);
                        wish_fwd = (uc.fwd / scale).clamp(-1.0, 1.0);
                        wish_side = (uc.side / scale).clamp(-1.0, 1.0);
                    } else {
                        wish_fwd = 0.0;
                        wish_side = 0.0;
                    }
                    input_source = 1;
                    buttons = uc.buttons;
                    aligned_usercmd_frames += 1;

                    let imag = (inv_fwd * inv_fwd + inv_side * inv_side).sqrt();
                    if next_sim.is_some() && geo.wall_contact == 0 {
                        if umag > 10.0 {
                            if on_ground || has_dv {
                                if imag > 1e-3 {
                                    let uf = uc.fwd / umag;
                                    let us = uc.side / umag;
                                    let invf = inv_fwd / imag;
                                    let invs = inv_side / imag;
                                    let cos = (uf * invf + us * invs).clamp(-1.0, 1.0);
                                    let ang_deg = cos.acos().to_degrees();
                                    inv_eval_frames += 1;
                                    inv_cos_sum += cos as f64;
                                    inv_ang_sum += ang_deg as f64;
                                    if ang_deg <= 45.0 {
                                        inv_agree_45 += 1;
                                    }
                                    if on_ground {
                                        inv_g_eval_frames += 1;
                                        inv_g_cos_sum += cos as f64;
                                        if ang_deg <= 45.0 {
                                            inv_g_agree_45 += 1;
                                        }
                                    }
                                } else {
                                    inv_eval_frames += 1;
                                    inv_ang_sum += 90.0;
                                    if on_ground {
                                        inv_g_eval_frames += 1;
                                    }
                                }
                            }
                        } else {
                            idle_total += 1;
                            if imag < 1e-3 {
                                idle_agree += 1;
                            }
                        }
                    }
                }
            }

            let rec = FrameRecord {
                steam_id: f.steam_id,
                demo_tick: f.demo_tick,
                server_tick: f.server_tick,
                origin: f.origin,
                velocity: f.velocity,
                eye_angles: f.eye_angles,
                wish_fwd,
                wish_side,
                maxspeed: f.maxspeed,
                sim_time: f.sim_time,
                ground_dist: geo.ground_dist,
                ground_normal: geo.ground_normal,
                ceiling_dist: geo.ceiling_dist,
                wall_dists: geo.wall_dists,
                ledge_depths: geo.ledge_depths,
                player_cond: f.player_cond,
                flags: f.flags,
                buttons,
                active_weapon: f.active_weapon,
                entity_id: f.entity_id,
                health: f.health,
                max_health: f.max_health,
                team: f.team,
                class_id: f.class_id,
                water_level: f.water_level,
                air_dash: f.air_dash,
                air_ducked: f.air_ducked,
                jumping: f.jumping,
                vel_source: f.vel_source,
                input_source,
                wall_contact: geo.wall_contact,
                pad: [0; 5],
            };

            let raw_bytes: &[u8; std::mem::size_of::<FrameRecord>()] =
                unsafe { &*((&rec) as *const FrameRecord as *const [u8; std::mem::size_of::<FrameRecord>()]) };
            writer.write_all(raw_bytes).map_err(|e| e.to_string())?;
            total_frames += 1;
        }
    }
    writer.flush().map_err(|e| e.to_string())?;

    let mut ground_sorted = ground_dists.clone();
    ground_sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let ground_frames = ground_sorted.len();
    let ground_dist_median = if ground_frames > 0 {
        ground_sorted[ground_frames / 2]
    } else {
        0.0
    };
    let ground_dist_p95 = if ground_frames > 0 {
        ground_sorted[(ground_frames * 95) / 100]
    } else {
        0.0
    };

    let meta = ExtractMeta {
        demo_path: demo_path.display().to_string(),
        bsp_path: bsp_path.display().to_string(),
        map: header.map,
        nick: header.nick,
        is_stv,
        local_entity_id: local_eid,
        interval_per_tick,
        total_frames,
        usercmd_packets: usercmds.len(),
        aligned_usercmd_frames,
        usercmd_lag_ticks: best_lag,
        inv_phys_eval_frames: inv_eval_frames,
        inv_phys_mean_cos_sim: if inv_eval_frames > 0 {
            (inv_cos_sum / inv_eval_frames as f64) as f32
        } else {
            0.0
        },
        inv_phys_mean_ang_err_deg: if inv_eval_frames > 0 {
            (inv_ang_sum / inv_eval_frames as f64) as f32
        } else {
            0.0
        },
        inv_phys_dir_agree_45deg: if inv_eval_frames > 0 {
            (inv_agree_45 as f32) / (inv_eval_frames as f32)
        } else {
            0.0
        },
        inv_phys_ground_eval_frames: inv_g_eval_frames,
        inv_phys_ground_cos_sim: if inv_g_eval_frames > 0 {
            (inv_g_cos_sum / inv_g_eval_frames as f64) as f32
        } else {
            0.0
        },
        inv_phys_ground_agree_45deg: if inv_g_eval_frames > 0 {
            (inv_g_agree_45 as f32) / (inv_g_eval_frames as f32)
        } else {
            0.0
        },
        inv_phys_idle_agree: if idle_total > 0 {
            (idle_agree as f32) / (idle_total as f32)
        } else {
            0.0
        },
        ground_frames,
        ground_dist_median,
        ground_dist_p95,
        ground_nz_ge_07_frac: if ground_frames > 0 {
            (ground_nz_ok as f32) / (ground_frames as f32)
        } else {
            0.0
        },
        record_size_bytes: std::mem::size_of::<FrameRecord>(),
    };

    let meta_path = out_bin_path.with_extension("meta.json");
    let meta_json = serde_json::to_string_pretty(&meta).map_err(|e| e.to_string())?;
    fs::write(&meta_path, meta_json).map_err(|e| e.to_string())?;
    Ok(meta)
}

fn main() {
    let cli = Cli::parse();
    match cli.cmd {
        Commands::Extract { demo, bsp, out } => {
            match extract_one(&demo, &bsp, &out) {
                Ok(meta) => {
                    println!("{}", serde_json::to_string_pretty(&meta).unwrap());
                }
                Err(e) => {
                    eprintln!("Error: {}", e);
                    std::process::exit(1);
                }
            }
        }
        Commands::Batch { manifest, out_dir, jobs } => {
            let txt = fs::read_to_string(&manifest).expect("read manifest");
            let mf: ManifestFile = serde_json::from_str(&txt).expect("parse manifest");
            fs::create_dir_all(&out_dir).expect("create out_dir");
            let pool = rayon::ThreadPoolBuilder::new().num_threads(jobs).build().unwrap();
            let done_cnt = std::sync::atomic::AtomicUsize::new(0);
            let total_n = mf.demos.len();
            let results: Vec<Result<ExtractMeta, String>> = pool.install(|| {
                mf.demos
                    .par_iter()
                    .map(|entry| {
                        let out_bin = out_dir.join(format!("{}.bin", entry.id));
                        let out_meta = out_bin.with_extension("meta.json");
                        let res = if out_bin.exists() && out_meta.exists() {
                            if let Ok(mtxt) = fs::read_to_string(&out_meta) {
                                if let Ok(m) = serde_json::from_str::<ExtractMeta>(&mtxt) {
                                    Ok(m)
                                } else {
                                    extract_one(Path::new(&entry.local_path), Path::new(&entry.bsp_path), &out_bin)
                                }
                            } else {
                                extract_one(Path::new(&entry.local_path), Path::new(&entry.bsp_path), &out_bin)
                            }
                        } else {
                            extract_one(Path::new(&entry.local_path), Path::new(&entry.bsp_path), &out_bin)
                        };
                        let n = done_cnt.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
                        if let Ok(ref m) = res {
                            println!(
                                "[{}/{}] {} map={} stv={} frames={} ucmd={} g_nz={:.3}",
                                n, total_n, entry.id, m.map, m.is_stv, m.total_frames, m.aligned_usercmd_frames, m.ground_nz_ge_07_frac
                            );
                        } else if let Err(ref e) = res {
                            eprintln!("[{}/{}] {} FAILED: {}", n, total_n, entry.id, e);
                        }
                        res
                    })
                    .collect()
            });
            let mut ok = 0usize;
            let mut err = 0usize;
            let mut total_frames = 0usize;
            let mut total_ucmd = 0usize;
            let mut metas: Vec<ExtractMeta> = Vec::new();
            for r in results {
                match r {
                    Ok(m) => {
                        ok += 1;
                        total_frames += m.total_frames;
                        total_ucmd += m.aligned_usercmd_frames;
                        metas.push(m);
                    }
                    Err(_) => {
                        err += 1;
                    }
                }
            }
            let summary = serde_json::json!({
                "ok_demos": ok,
                "failed_demos": err,
                "total_frames": total_frames,
                "total_aligned_usercmd_frames": total_ucmd,
                "demos": metas,
            });
            let _ = fs::write(out_dir.join("batch_summary.json"), serde_json::to_string_pretty(&summary).unwrap());
            println!("Batch complete: ok={} err={} total_frames={} total_ucmd={}", ok, err, total_frames, total_ucmd);
        }
    }
}
