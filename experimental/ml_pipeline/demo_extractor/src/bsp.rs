use byteorder::{LittleEndian, ReadBytesExt};
use std::fs::File;
use std::io::{Cursor, Read, Seek, SeekFrom};
use std::path::Path;

pub const CONTENTS_SOLID: i32 = 0x1;
pub const CONTENTS_WINDOW: i32 = 0x2;
pub const CONTENTS_GRATE: i32 = 0x8;
pub const CONTENTS_MOVEABLE: i32 = 0x4000;
pub const CONTENTS_PLAYERCLIP: i32 = 0x10000;
pub const MASK_PLAYERSOLID_BRUSHONLY: i32 =
    CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE | CONTENTS_MOVEABLE | CONTENTS_PLAYERCLIP;

const DIST_EPSILON: f32 = 0.03125;
const NEVER_UPDATED: f32 = -9999.0;
const DISPTRI_TAG_REMOVE: u16 = 1 << 5;

const LUMP_PLANES: usize = 1;
const LUMP_VERTEXES: usize = 3;
const LUMP_NODES: usize = 5;
const LUMP_FACES: usize = 7;
const LUMP_LEAFS: usize = 10;
const LUMP_EDGES: usize = 12;
const LUMP_SURFEDGES: usize = 13;
const LUMP_MODELS: usize = 14;
const LUMP_LEAFBRUSHES: usize = 17;
const LUMP_BRUSHES: usize = 18;
const LUMP_BRUSHSIDES: usize = 19;
const LUMP_DISPINFO: usize = 26;
const LUMP_DISP_VERTS: usize = 33;
const LUMP_DISP_TRIS: usize = 48;

#[derive(Clone, Copy, Default)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    #[inline(always)]
    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }
    #[inline(always)]
    pub fn dot(self, o: Self) -> f32 {
        self.x * o.x + self.y * o.y + self.z * o.z
    }
    #[inline(always)]
    pub fn sub(self, o: Self) -> Self {
        Self::new(self.x - o.x, self.y - o.y, self.z - o.z)
    }
    #[inline(always)]
    pub fn add(self, o: Self) -> Self {
        Self::new(self.x + o.x, self.y + o.y, self.z + o.z)
    }
    #[inline(always)]
    pub fn scale(self, s: f32) -> Self {
        Self::new(self.x * s, self.y * s, self.z * s)
    }
    #[inline(always)]
    pub fn cross(self, o: Self) -> Self {
        Self::new(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x,
        )
    }
    #[inline(always)]
    pub fn length_sq(self) -> f32 {
        self.dot(self)
    }
    #[inline(always)]
    pub fn normalize(self) -> Self {
        let len = self.length_sq().sqrt();
        if len > 1e-8 {
            self.scale(1.0 / len)
        } else {
            Self::default()
        }
    }
}

#[derive(Clone, Copy)]
struct Plane {
    normal: Vec3,
    dist: f32,
    plane_type: i32,
}

#[derive(Clone, Copy)]
struct Node {
    planenum: usize,
    children: [i32; 2],
}

#[derive(Clone, Copy)]
struct Leaf {
    firstleafbrush: usize,
    numleafbrushes: usize,
}

#[derive(Clone, Copy)]
struct Brush {
    firstside: usize,
    numsides: usize,
    contents: i32,
}

#[derive(Clone, Copy)]
struct BrushSide {
    planenum: usize,
    bevel: i16,
}

#[derive(Clone, Copy)]
struct DispTri {
    v0: Vec3,
    v1: Vec3,
    v2: Vec3,
    normal: Vec3,
    dist: f32,
}

struct DispPatch {
    mins: Vec3,
    maxs: Vec3,
    tris: Vec<DispTri>,
}

#[derive(Clone, Copy)]
pub struct TraceResult {
    pub fraction: f32,
    pub normal: Vec3,
    pub start_solid: bool,
    pub all_solid: bool,
    pub hit: bool,
}

impl Default for TraceResult {
    fn default() -> Self {
        Self {
            fraction: 1.0,
            normal: Vec3::default(),
            start_solid: false,
            all_solid: false,
            hit: false,
        }
    }
}

#[derive(Clone, Copy, Default)]
pub struct GeoProbes {
    pub ground_dist: f32,
    pub ground_normal: [f32; 3],
    pub ceiling_dist: f32,
    pub wall_dists: [f32; 8],
    pub ledge_depths: [f32; 8],
    pub wall_contact: u8,
}

const DISP_GRID_DIM: usize = 64;

pub struct BspMap {
    planes: Vec<Plane>,
    nodes: Vec<Node>,
    leafs: Vec<Leaf>,
    leafbrushes: Vec<u16>,
    brushes: Vec<Brush>,
    brushsides: Vec<BrushSide>,
    headnode: i32,
    disps: Vec<DispPatch>,
    disp_grid_min: [f32; 2],
    disp_grid_cell: [f32; 2],
    disp_grid: Vec<Vec<u32>>,
}

fn read_lump_bytes(file: &mut File, ofs: i32, len: i32) -> Result<Vec<u8>, String> {
    if len <= 0 || ofs <= 0 {
        return Ok(Vec::new());
    }
    file.seek(SeekFrom::Start(ofs as u64)).map_err(|e| e.to_string())?;
    let mut raw = vec![0u8; len as usize];
    file.read_exact(&mut raw).map_err(|e| e.to_string())?;
    if raw.len() >= 17 && &raw[0..4] == b"LZMA" {
        let mut hdr = Cursor::new(&raw[4..12]);
        let actual_size = hdr.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let _lzma_size = hdr.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let mut std_lzma = Vec::with_capacity(13 + raw.len() - 17);
        std_lzma.extend_from_slice(&raw[12..17]);
        std_lzma.extend_from_slice(&(actual_size as u64).to_le_bytes());
        std_lzma.extend_from_slice(&raw[17..]);
        let mut out = Vec::with_capacity(actual_size as usize);
        lzma_rs::lzma_decompress(&mut Cursor::new(&std_lzma), &mut out).map_err(|e| format!("{:?}", e))?;
        Ok(out)
    } else {
        Ok(raw)
    }
}

impl BspMap {
    pub fn load(path: &Path) -> Result<Self, String> {
        let mut file = File::open(path).map_err(|e| e.to_string())?;
        let mut ident = [0u8; 4];
        file.read_exact(&mut ident).map_err(|e| e.to_string())?;
        if &ident != b"VBSP" {
            return Err("not a VBSP file".to_string());
        }
        let version = file.read_i32::<LittleEndian>().map_err(|e| e.to_string())?;
        if version < 19 || version > 21 {
            return Err(format!("unsupported BSP version {}", version));
        }

        let mut lumps = [(0i32, 0i32, 0i32, 0i32); 64];
        for l in &mut lumps {
            l.0 = file.read_i32::<LittleEndian>().map_err(|e| e.to_string())?;
            l.1 = file.read_i32::<LittleEndian>().map_err(|e| e.to_string())?;
            l.2 = file.read_i32::<LittleEndian>().map_err(|e| e.to_string())?;
            l.3 = file.read_i32::<LittleEndian>().map_err(|e| e.to_string())?;
        }

        let planes_raw = read_lump_bytes(&mut file, lumps[LUMP_PLANES].0, lumps[LUMP_PLANES].1)?;
        let mut planes = Vec::with_capacity(planes_raw.len() / 20);
        let mut c = Cursor::new(&planes_raw);
        for _ in 0..(planes_raw.len() / 20) {
            let nx = c.read_f32::<LittleEndian>().unwrap();
            let ny = c.read_f32::<LittleEndian>().unwrap();
            let nz = c.read_f32::<LittleEndian>().unwrap();
            let dist = c.read_f32::<LittleEndian>().unwrap();
            let plane_type = c.read_i32::<LittleEndian>().unwrap();
            planes.push(Plane {
                normal: Vec3::new(nx, ny, nz),
                dist,
                plane_type,
            });
        }

        let nodes_raw = read_lump_bytes(&mut file, lumps[LUMP_NODES].0, lumps[LUMP_NODES].1)?;
        let mut nodes = Vec::with_capacity(nodes_raw.len() / 32);
        let mut c = Cursor::new(&nodes_raw);
        for _ in 0..(nodes_raw.len() / 32) {
            let planenum = c.read_i32::<LittleEndian>().unwrap() as usize;
            let c0 = c.read_i32::<LittleEndian>().unwrap();
            let c1 = c.read_i32::<LittleEndian>().unwrap();
            c.seek(SeekFrom::Current(20)).unwrap();
            nodes.push(Node {
                planenum,
                children: [c0, c1],
            });
        }

        let leafs_raw = read_lump_bytes(&mut file, lumps[LUMP_LEAFS].0, lumps[LUMP_LEAFS].1)?;
        let leaf_stride = if lumps[LUMP_LEAFS].2 == 0 { 56usize } else { 32usize };
        let mut leafs = Vec::with_capacity(leafs_raw.len() / leaf_stride);
        let mut c = Cursor::new(&leafs_raw);
        for i in 0..(leafs_raw.len() / leaf_stride) {
            c.seek(SeekFrom::Start((i * leaf_stride + 24) as u64)).unwrap();
            let firstleafbrush = c.read_u16::<LittleEndian>().unwrap() as usize;
            let numleafbrushes = c.read_u16::<LittleEndian>().unwrap() as usize;
            leafs.push(Leaf {
                firstleafbrush,
                numleafbrushes,
            });
        }

        let lb_raw = read_lump_bytes(&mut file, lumps[LUMP_LEAFBRUSHES].0, lumps[LUMP_LEAFBRUSHES].1)?;
        let mut leafbrushes = Vec::with_capacity(lb_raw.len() / 2);
        let mut c = Cursor::new(&lb_raw);
        for _ in 0..(lb_raw.len() / 2) {
            leafbrushes.push(c.read_u16::<LittleEndian>().unwrap());
        }

        let brushes_raw = read_lump_bytes(&mut file, lumps[LUMP_BRUSHES].0, lumps[LUMP_BRUSHES].1)?;
        let mut brushes = Vec::with_capacity(brushes_raw.len() / 12);
        let mut c = Cursor::new(&brushes_raw);
        for _ in 0..(brushes_raw.len() / 12) {
            let firstside = c.read_i32::<LittleEndian>().unwrap() as usize;
            let numsides = c.read_i32::<LittleEndian>().unwrap() as usize;
            let contents = c.read_i32::<LittleEndian>().unwrap();
            brushes.push(Brush {
                firstside,
                numsides,
                contents,
            });
        }

        let bs_raw = read_lump_bytes(&mut file, lumps[LUMP_BRUSHSIDES].0, lumps[LUMP_BRUSHSIDES].1)?;
        let mut brushsides = Vec::with_capacity(bs_raw.len() / 8);
        let mut c = Cursor::new(&bs_raw);
        for _ in 0..(bs_raw.len() / 8) {
            let planenum = c.read_u16::<LittleEndian>().unwrap() as usize;
            let _texinfo = c.read_i16::<LittleEndian>().unwrap();
            let _dispinfo = c.read_i16::<LittleEndian>().unwrap();
            let bevel = c.read_i16::<LittleEndian>().unwrap();
            brushsides.push(BrushSide { planenum, bevel });
        }

        let models_raw = read_lump_bytes(&mut file, lumps[LUMP_MODELS].0, lumps[LUMP_MODELS].1)?;
        let headnode = if models_raw.len() >= 48 {
            let mut c = Cursor::new(&models_raw[36..40]);
            c.read_i32::<LittleEndian>().unwrap()
        } else {
            0
        };

        let disps = Self::load_displacements(&mut file, &lumps)?;
        let (disp_grid_min, disp_grid_cell, disp_grid) = Self::build_disp_grid(&disps);

        Ok(Self {
            planes,
            nodes,
            leafs,
            leafbrushes,
            brushes,
            brushsides,
            headnode,
            disps,
            disp_grid_min,
            disp_grid_cell,
            disp_grid,
        })
    }

    fn build_disp_grid(disps: &[DispPatch]) -> ([f32; 2], [f32; 2], Vec<Vec<u32>>) {
        let mut grid = vec![Vec::new(); DISP_GRID_DIM * DISP_GRID_DIM];
        if disps.is_empty() {
            return ([0.0, 0.0], [1.0, 1.0], grid);
        }
        let mut min_x = f32::MAX;
        let mut min_y = f32::MAX;
        let mut max_x = f32::MIN;
        let mut max_y = f32::MIN;
        for d in disps {
            min_x = min_x.min(d.mins.x);
            min_y = min_y.min(d.mins.y);
            max_x = max_x.max(d.maxs.x);
            max_y = max_y.max(d.maxs.y);
        }
        let cell_x = ((max_x - min_x).max(64.0)) / (DISP_GRID_DIM as f32);
        let cell_y = ((max_y - min_y).max(64.0)) / (DISP_GRID_DIM as f32);
        for (idx, d) in disps.iter().enumerate() {
            let x0 = (((d.mins.x - min_x) / cell_x).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
            let x1 = (((d.maxs.x - min_x) / cell_x).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
            let y0 = (((d.mins.y - min_y) / cell_y).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
            let y1 = (((d.maxs.y - min_y) / cell_y).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
            for gy in y0..=y1 {
                for gx in x0..=x1 {
                    grid[gy * DISP_GRID_DIM + gx].push(idx as u32);
                }
            }
        }
        ([min_x, min_y], [cell_x, cell_y], grid)
    }

    fn load_displacements(file: &mut File, lumps: &[(i32, i32, i32, i32); 64]) -> Result<Vec<DispPatch>, String> {
        let disp_raw = read_lump_bytes(file, lumps[LUMP_DISPINFO].0, lumps[LUMP_DISPINFO].1)?;
        if disp_raw.is_empty() {
            return Ok(Vec::new());
        }
        let verts_raw = read_lump_bytes(file, lumps[LUMP_VERTEXES].0, lumps[LUMP_VERTEXES].1)?;
        let edges_raw = read_lump_bytes(file, lumps[LUMP_EDGES].0, lumps[LUMP_EDGES].1)?;
        let surfedges_raw = read_lump_bytes(file, lumps[LUMP_SURFEDGES].0, lumps[LUMP_SURFEDGES].1)?;
        let faces_raw = read_lump_bytes(file, lumps[LUMP_FACES].0, lumps[LUMP_FACES].1)?;
        let dverts_raw = read_lump_bytes(file, lumps[LUMP_DISP_VERTS].0, lumps[LUMP_DISP_VERTS].1)?;
        let dtris_raw = read_lump_bytes(file, lumps[LUMP_DISP_TRIS].0, lumps[LUMP_DISP_TRIS].1)?;

        let mut verts = Vec::with_capacity(verts_raw.len() / 12);
        let mut c = Cursor::new(&verts_raw);
        for _ in 0..(verts_raw.len() / 12) {
            verts.push(Vec3::new(
                c.read_f32::<LittleEndian>().unwrap(),
                c.read_f32::<LittleEndian>().unwrap(),
                c.read_f32::<LittleEndian>().unwrap(),
            ));
        }

        let mut edges = Vec::with_capacity(edges_raw.len() / 4);
        let mut c = Cursor::new(&edges_raw);
        for _ in 0..(edges_raw.len() / 4) {
            edges.push([c.read_u16::<LittleEndian>().unwrap(), c.read_u16::<LittleEndian>().unwrap()]);
        }

        let mut surfedges = Vec::with_capacity(surfedges_raw.len() / 4);
        let mut c = Cursor::new(&surfedges_raw);
        for _ in 0..(surfedges_raw.len() / 4) {
            surfedges.push(c.read_i32::<LittleEndian>().unwrap());
        }

        let mut faces = Vec::with_capacity(faces_raw.len() / 56);
        for i in 0..(faces_raw.len() / 56) {
            let mut c = Cursor::new(&faces_raw[(i * 56 + 4)..(i * 56 + 10)]);
            let firstedge = c.read_i32::<LittleEndian>().unwrap();
            let numedges = c.read_i16::<LittleEndian>().unwrap();
            faces.push((firstedge, numedges));
        }

        let mut disp_verts = Vec::with_capacity(dverts_raw.len() / 20);
        let mut c = Cursor::new(&dverts_raw);
        for _ in 0..(dverts_raw.len() / 20) {
            let vx = c.read_f32::<LittleEndian>().unwrap();
            let vy = c.read_f32::<LittleEndian>().unwrap();
            let vz = c.read_f32::<LittleEndian>().unwrap();
            let dist = c.read_f32::<LittleEndian>().unwrap();
            let _alpha = c.read_f32::<LittleEndian>().unwrap();
            disp_verts.push((Vec3::new(vx, vy, vz), dist));
        }

        let mut disp_tris = Vec::with_capacity(dtris_raw.len() / 2);
        let mut c = Cursor::new(&dtris_raw);
        for _ in 0..(dtris_raw.len() / 2) {
            disp_tris.push(c.read_u16::<LittleEndian>().unwrap());
        }

        let mut patches = Vec::with_capacity(disp_raw.len() / 176);
        for i in 0..(disp_raw.len() / 176) {
            let base = i * 176;
            let mut c = Cursor::new(&disp_raw[base..(base + 38)]);
            let sx = c.read_f32::<LittleEndian>().unwrap();
            let sy = c.read_f32::<LittleEndian>().unwrap();
            let sz = c.read_f32::<LittleEndian>().unwrap();
            let start_pos = Vec3::new(sx, sy, sz);
            let vert_start = c.read_i32::<LittleEndian>().unwrap() as usize;
            let tri_start = c.read_i32::<LittleEndian>().unwrap() as usize;
            let power = c.read_i32::<LittleEndian>().unwrap();
            let _min_tess = c.read_i32::<LittleEndian>().unwrap();
            let _smooth = c.read_f32::<LittleEndian>().unwrap();
            let contents = c.read_i32::<LittleEndian>().unwrap();
            let map_face = c.read_u16::<LittleEndian>().unwrap() as usize;

            if power < 2 || power > 4 || map_face >= faces.len() {
                continue;
            }
            if (contents & (CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_MOVEABLE | CONTENTS_WINDOW)) == 0 && contents != 0 {
                continue;
            }
            let (firstedge, numedges) = faces[map_face];
            if numedges != 4 || firstedge < 0 || (firstedge as usize + 4) > surfedges.len() {
                continue;
            }
            let mut corner_pts = [Vec3::default(); 4];
            let mut valid = true;
            for k in 0..4 {
                let se = surfedges[firstedge as usize + k];
                let e_idx = se.unsigned_abs() as usize;
                if e_idx >= edges.len() {
                    valid = false;
                    break;
                }
                let v_idx = if se < 0 { edges[e_idx][1] } else { edges[e_idx][0] } as usize;
                if v_idx >= verts.len() {
                    valid = false;
                    break;
                }
                corner_pts[k] = verts[v_idx];
            }
            if !valid {
                continue;
            }

            let mut start_idx = 0usize;
            let mut best_d2 = f32::MAX;
            for k in 0..4 {
                let d2 = corner_pts[k].sub(start_pos).length_sq();
                if d2 < best_d2 {
                    best_d2 = d2;
                    start_idx = k;
                }
            }
            let p0 = corner_pts[start_idx];
            let p1 = corner_pts[(start_idx + 1) % 4];
            let p2 = corner_pts[(start_idx + 2) % 4];
            let p3 = corner_pts[(start_idx + 3) % 4];

            let side_len = (1usize << power) + 1;
            let num_verts = side_len * side_len;
            if vert_start + num_verts > disp_verts.len() {
                continue;
            }

            let oo_int = 1.0 / ((side_len - 1) as f32);
            let edge_int0 = p1.sub(p0).scale(oo_int);
            let edge_int1 = p2.sub(p3).scale(oo_int);

            let mut grid = Vec::with_capacity(num_verts);
            let mut mins = Vec3::new(f32::MAX, f32::MAX, f32::MAX);
            let mut maxs = Vec3::new(f32::MIN, f32::MIN, f32::MIN);
            for row in 0..side_len {
                let ep0 = p0.add(edge_int0.scale(row as f32));
                let ep1 = p3.add(edge_int1.scale(row as f32));
                let seg_int = ep1.sub(ep0).scale(oo_int);
                for col in 0..side_len {
                    let idx = row * side_len + col;
                    let base_pt = ep0.add(seg_int.scale(col as f32));
                    let (dv, ddist) = disp_verts[vert_start + idx];
                    let pt = base_pt.add(dv.scale(ddist));
                    mins.x = mins.x.min(pt.x);
                    mins.y = mins.y.min(pt.y);
                    mins.z = mins.z.min(pt.z);
                    maxs.x = maxs.x.max(pt.x);
                    maxs.y = maxs.y.max(pt.y);
                    maxs.z = maxs.z.max(pt.z);
                    grid.push(pt);
                }
            }

            let n_cells = side_len - 1;
            let mut tris = Vec::with_capacity(n_cells * n_cells * 2);
            for row in 0..n_cells {
                for col in 0..n_cells {
                    let ndx0 = row * side_len + col;
                    let ndx1 = ndx0 + side_len;
                    let ndx2 = ndx1 + 1;
                    let ndx3 = ndx0 + 1;
                    let t_base = tri_start + (row * n_cells + col) * 2;
                    let tag0 = disp_tris.get(t_base).copied().unwrap_or(0);
                    let tag1 = disp_tris.get(t_base + 1).copied().unwrap_or(0);

                    let odd = (ndx0 % 2) == 1;
                    let pairs = if odd {
                        [(ndx0, ndx1, ndx3), (ndx1, ndx2, ndx3)]
                    } else {
                        [(ndx0, ndx1, ndx2), (ndx0, ndx2, ndx3)]
                    };

                    for (ti, (a, b, d)) in pairs.into_iter().enumerate() {
                        let tag = if ti == 0 { tag0 } else { tag1 };
                        if (tag & DISPTRI_TAG_REMOVE) != 0 {
                            continue;
                        }
                        let v0 = grid[a];
                        let v1 = grid[b];
                        let v2 = grid[d];
                        let n = v2.sub(v0).cross(v1.sub(v0)).normalize();
                        if n.length_sq() > 0.5 {
                            tris.push(DispTri {
                                v0,
                                v1,
                                v2,
                                normal: n,
                                dist: n.dot(v0),
                            });
                        }
                    }
                }
            }
            if !tris.is_empty() {
                patches.push(DispPatch { mins, maxs, tris });
            }
        }
        Ok(patches)
    }

    #[inline]
    fn clip_box_to_brush(
        &self,
        trace: &mut TraceResult,
        mins: Vec3,
        maxs: Vec3,
        p1: Vec3,
        p2: Vec3,
        brush: &Brush,
        is_point: bool,
    ) {
        if brush.numsides == 0 {
            return;
        }
        let mut enterfrac = NEVER_UPDATED;
        let mut leavefrac = 1.0f32;
        let mut clip_normal = Vec3::default();
        let mut getout = false;
        let mut startout = false;

        for i in 0..brush.numsides {
            let side = &self.brushsides[brush.firstside + i];
            if is_point && side.bevel == 1 {
                continue;
            }
            let plane = &self.planes[side.planenum];
            let dist = if is_point {
                plane.dist
            } else {
                let ox = if plane.normal.x < 0.0 { maxs.x } else { mins.x };
                let oy = if plane.normal.y < 0.0 { maxs.y } else { mins.y };
                let oz = if plane.normal.z < 0.0 { maxs.z } else { mins.z };
                plane.dist - (ox * plane.normal.x + oy * plane.normal.y + oz * plane.normal.z)
            };

            let d1 = p1.dot(plane.normal) - dist;
            let d2 = p2.dot(plane.normal) - dist;
            if d1 > 0.0 && d2 > 0.0 {
                return;
            }
            if d2 > 0.0 {
                getout = true;
            }
            if d1 > 0.0 {
                startout = true;
            }
            if d1 <= 0.0 && d2 <= 0.0 {
                continue;
            }
            if d1 > d2 {
                let f = (d1 - DIST_EPSILON) / (d1 - d2);
                if f > enterfrac {
                    enterfrac = f;
                    clip_normal = plane.normal;
                }
            } else {
                let f = (d1 + DIST_EPSILON) / (d1 - d2);
                if f < leavefrac {
                    leavefrac = f;
                }
            }
        }

        if !startout {
            trace.start_solid = true;
            if !getout {
                trace.all_solid = true;
            }
            return;
        }
        if enterfrac < leavefrac && enterfrac > NEVER_UPDATED && enterfrac < trace.fraction {
            let clamped = if enterfrac < 0.0 { 0.0 } else { enterfrac };
            trace.fraction = clamped;
            trace.normal = clip_normal;
            trace.hit = true;
        }
    }

    fn trace_leaf(
        &self,
        leaf_idx: usize,
        mins: Vec3,
        maxs: Vec3,
        start: Vec3,
        end: Vec3,
        is_point: bool,
        mask: i32,
        trace: &mut TraceResult,
    ) {
        let leaf = &self.leafs[leaf_idx];
        for i in 0..leaf.numleafbrushes {
            let b_idx = self.leafbrushes[leaf.firstleafbrush + i] as usize;
            let brush = &self.brushes[b_idx];
            if (brush.contents & mask) == 0 {
                continue;
            }
            self.clip_box_to_brush(trace, mins, maxs, start, end, brush, is_point);
            if trace.all_solid {
                return;
            }
        }
    }

    fn recursive_hull_check(
        &self,
        node_idx: i32,
        p1f: f32,
        p2f: f32,
        p1: Vec3,
        p2: Vec3,
        start: Vec3,
        end: Vec3,
        mins: Vec3,
        maxs: Vec3,
        extents: Vec3,
        is_point: bool,
        mask: i32,
        trace: &mut TraceResult,
    ) {
        if trace.fraction <= p1f {
            return;
        }
        if node_idx < 0 {
            let leaf_idx = (-1 - node_idx) as usize;
            if leaf_idx < self.leafs.len() {
                self.trace_leaf(leaf_idx, mins, maxs, start, end, is_point, mask, trace);
            }
            return;
        }

        let node = &self.nodes[node_idx as usize];
        let plane = &self.planes[node.planenum];
        let (t1, t2, offset) = if plane.plane_type < 3 {
            let ax = plane.plane_type as usize;
            let (c1, c2, ex) = match ax {
                0 => (p1.x, p2.x, extents.x),
                1 => (p1.y, p2.y, extents.y),
                _ => (p1.z, p2.z, extents.z),
            };
            (c1 - plane.dist, c2 - plane.dist, ex)
        } else {
            let o = if is_point {
                0.0
            } else {
                extents.x * plane.normal.x.abs()
                    + extents.y * plane.normal.y.abs()
                    + extents.z * plane.normal.z.abs()
            };
            (p1.dot(plane.normal) - plane.dist, p2.dot(plane.normal) - plane.dist, o)
        };

        if t1 >= offset && t2 >= offset {
            self.recursive_hull_check(
                node.children[0], p1f, p2f, p1, p2, start, end, mins, maxs, extents, is_point, mask, trace,
            );
            return;
        }
        if t1 < -offset && t2 < -offset {
            self.recursive_hull_check(
                node.children[1], p1f, p2f, p1, p2, start, end, mins, maxs, extents, is_point, mask, trace,
            );
            return;
        }

        let (side, frac1, frac2) = if t1 < t2 {
            let idist = 1.0 / (t1 - t2);
            (
                1usize,
                ((t1 - offset - DIST_EPSILON) * idist).clamp(0.0, 1.0),
                ((t1 + offset + DIST_EPSILON) * idist).clamp(0.0, 1.0),
            )
        } else if t1 > t2 {
            let idist = 1.0 / (t1 - t2);
            (
                0usize,
                ((t1 + offset + DIST_EPSILON) * idist).clamp(0.0, 1.0),
                ((t1 - offset - DIST_EPSILON) * idist).clamp(0.0, 1.0),
            )
        } else {
            (0usize, 1.0, 0.0)
        };

        let midf1 = p1f + (p2f - p1f) * frac1;
        let mid1 = p1.add(p2.sub(p1).scale(frac1));
        self.recursive_hull_check(
            node.children[side], p1f, midf1, p1, mid1, start, end, mins, maxs, extents, is_point, mask, trace,
        );

        let midf2 = p1f + (p2f - p1f) * frac2;
        if midf2 < trace.fraction {
            let mid2 = p1.add(p2.sub(p1).scale(frac2));
            self.recursive_hull_check(
                node.children[side ^ 1], midf2, p2f, mid2, p2, start, end, mins, maxs, extents, is_point, mask, trace,
            );
        }
    }

    #[inline]
    fn ray_aabb_intersects(start: Vec3, inv_dir: Vec3, bmin: Vec3, bmax: Vec3, max_t: f32) -> bool {
        let tx1 = (bmin.x - start.x) * inv_dir.x;
        let tx2 = (bmax.x - start.x) * inv_dir.x;
        let mut tmin = tx1.min(tx2);
        let mut tmax = tx1.max(tx2);

        let ty1 = (bmin.y - start.y) * inv_dir.y;
        let ty2 = (bmax.y - start.y) * inv_dir.y;
        tmin = tmin.max(ty1.min(ty2));
        tmax = tmax.min(ty1.max(ty2));

        let tz1 = (bmin.z - start.z) * inv_dir.z;
        let tz2 = (bmax.z - start.z) * inv_dir.z;
        tmin = tmin.max(tz1.min(tz2));
        tmax = tmax.min(tz1.max(tz2));

        tmax >= 0.0 && tmin <= tmax && tmin <= max_t
    }

    #[inline]
    fn ray_triangle(orig: Vec3, dir: Vec3, tri: &DispTri) -> Option<f32> {
        let e1 = tri.v1.sub(tri.v0);
        let e2 = tri.v2.sub(tri.v0);
        let h = dir.cross(e2);
        let a = e1.dot(h);
        if a.abs() < 1e-7 {
            return None;
        }
        let f = 1.0 / a;
        let s = orig.sub(tri.v0);
        let u = f * s.dot(h);
        if u < 0.0 || u > 1.0 {
            return None;
        }
        let q = s.cross(e1);
        let v = f * dir.dot(q);
        if v < 0.0 || u + v > 1.0 {
            return None;
        }
        let t = f * e2.dot(q);
        if t >= 0.0 && t <= 1.0 {
            Some(t)
        } else {
            None
        }
    }

    fn trace_displacements_ray(&self, start: Vec3, end: Vec3, trace: &mut TraceResult) {
        if self.disps.is_empty() {
            return;
        }
        let dir = end.sub(start);
        let inv_dir = Vec3::new(
            if dir.x.abs() > 1e-9 { 1.0 / dir.x } else { 1e9 },
            if dir.y.abs() > 1e-9 { 1.0 / dir.y } else { 1e9 },
            if dir.z.abs() > 1e-9 { 1.0 / dir.z } else { 1e9 },
        );
        let rx0 = start.x.min(end.x);
        let rx1 = start.x.max(end.x);
        let ry0 = start.y.min(end.y);
        let ry1 = start.y.max(end.y);
        let gx0 = (((rx0 - self.disp_grid_min[0]) / self.disp_grid_cell[0]).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
        let gx1 = (((rx1 - self.disp_grid_min[0]) / self.disp_grid_cell[0]).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
        let gy0 = (((ry0 - self.disp_grid_min[1]) / self.disp_grid_cell[1]).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
        let gy1 = (((ry1 - self.disp_grid_min[1]) / self.disp_grid_cell[1]).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;

        for gy in gy0..=gy1 {
            for gx in gx0..=gx1 {
                for &didx in &self.disp_grid[gy * DISP_GRID_DIM + gx] {
                    let disp = &self.disps[didx as usize];
                    if !Self::ray_aabb_intersects(start, inv_dir, disp.mins, disp.maxs, trace.fraction) {
                        continue;
                    }
                    for tri in &disp.tris {
                        if let Some(t) = Self::ray_triangle(start, dir, tri) {
                            if t < trace.fraction {
                                trace.fraction = t;
                                trace.normal = if tri.normal.dot(dir) > 0.0 {
                                    tri.normal.scale(-1.0)
                                } else {
                                    tri.normal
                                };
                                trace.hit = true;
                            }
                        }
                    }
                }
            }
        }
    }

    fn trace_displacements_vertical_hull(
        &self,
        start_z: f32,
        end_z: f32,
        cx: f32,
        cy: f32,
        half_w: f32,
        hull_z_min: f32,
        hull_z_max: f32,
        trace: &mut TraceResult,
    ) {
        if self.disps.is_empty() {
            return;
        }
        let dz = end_z - start_z;
        if dz.abs() < 1e-6 {
            return;
        }
        let down = dz < 0.0;
        let bx0 = cx - half_w;
        let bx1 = cx + half_w;
        let by0 = cy - half_w;
        let by1 = cy + half_w;
        let z_lo = start_z.min(end_z) + hull_z_min;
        let z_hi = start_z.max(end_z) + hull_z_max;

        let gx0 = (((bx0 - self.disp_grid_min[0]) / self.disp_grid_cell[0]).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
        let gx1 = (((bx1 - self.disp_grid_min[0]) / self.disp_grid_cell[0]).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
        let gy0 = (((by0 - self.disp_grid_min[1]) / self.disp_grid_cell[1]).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;
        let gy1 = (((by1 - self.disp_grid_min[1]) / self.disp_grid_cell[1]).floor() as isize).clamp(0, (DISP_GRID_DIM - 1) as isize) as usize;

        for gy in gy0..=gy1 {
            for gx in gx0..=gx1 {
                for &didx in &self.disp_grid[gy * DISP_GRID_DIM + gx] {
                    let disp = &self.disps[didx as usize];
                    if disp.maxs.x < bx0 || disp.mins.x > bx1 || disp.maxs.y < by0 || disp.mins.y > by1 || disp.maxs.z < z_lo || disp.mins.z > z_hi {
                        continue;
                    }
                    for tri in &disp.tris {
                        if tri.normal.z.abs() < 0.05 {
                            continue;
                        }
                        let tmin_x = tri.v0.x.min(tri.v1.x).min(tri.v2.x);
                        let tmax_x = tri.v0.x.max(tri.v1.x).max(tri.v2.x);
                        let tmin_y = tri.v0.y.min(tri.v1.y).min(tri.v2.y);
                        let tmax_y = tri.v0.y.max(tri.v1.y).max(tri.v2.y);
                        if tmax_x < bx0 || tmin_x > bx1 || tmax_y < by0 || tmin_y > by1 {
                            continue;
                        }
                        let mut best_t = trace.fraction;
                        let sample_pts = [
                            (cx, cy),
                            (bx0 + 1.0, by0 + 1.0),
                            (bx1 - 1.0, by0 + 1.0),
                            (bx0 + 1.0, by1 - 1.0),
                            (bx1 - 1.0, by1 - 1.0),
                            (tri.v0.x.clamp(bx0, bx1), tri.v0.y.clamp(by0, by1)),
                            (tri.v1.x.clamp(bx0, bx1), tri.v1.y.clamp(by0, by1)),
                            (tri.v2.x.clamp(bx0, bx1), tri.v2.y.clamp(by0, by1)),
                        ];
                        for (px, py) in sample_pts {
                            if !point_in_tri_xy(px, py, tri.v0, tri.v1, tri.v2) {
                                continue;
                            }
                            let tz = (tri.dist - tri.normal.x * px - tri.normal.y * py) / tri.normal.z;
                            let t = if down {
                                (start_z + hull_z_min - tz) / (-dz)
                            } else {
                                (tz - (start_z + hull_z_max)) / dz
                            };
                            if t >= -0.02 && t < best_t {
                                best_t = t.max(0.0);
                                trace.fraction = best_t;
                                trace.normal = if down && tri.normal.z < 0.0 {
                                    tri.normal.scale(-1.0)
                                } else if !down && tri.normal.z > 0.0 {
                                    tri.normal.scale(-1.0)
                                } else {
                                    tri.normal
                                };
                                trace.hit = true;
                            }
                        }
                    }
                }
            }
        }
    }

    pub fn trace_ray(&self, start: Vec3, end: Vec3) -> TraceResult {
        let mut trace = TraceResult::default();
        self.recursive_hull_check(
            self.headnode,
            0.0,
            1.0,
            start,
            end,
            start,
            end,
            Vec3::default(),
            Vec3::default(),
            Vec3::default(),
            true,
            MASK_PLAYERSOLID_BRUSHONLY,
            &mut trace,
        );
        self.trace_displacements_ray(start, end, &mut trace);
        trace
    }

    pub fn trace_hull(&self, start: Vec3, end: Vec3, mins: Vec3, maxs: Vec3) -> TraceResult {
        let center_offset = mins.add(maxs).scale(0.5);
        let extents = maxs.sub(mins).scale(0.5);
        let c_start = start.add(center_offset);
        let c_end = end.add(center_offset);
        let hull_mins = extents.scale(-1.0);
        let hull_maxs = extents;

        let mut trace = TraceResult::default();
        self.recursive_hull_check(
            self.headnode,
            0.0,
            1.0,
            c_start,
            c_end,
            c_start,
            c_end,
            hull_mins,
            hull_maxs,
            extents,
            false,
            MASK_PLAYERSOLID_BRUSHONLY,
            &mut trace,
        );
        if (end.x - start.x).abs() < 1e-4 && (end.y - start.y).abs() < 1e-4 {
            self.trace_displacements_vertical_hull(
                start.z,
                end.z,
                start.x,
                start.y,
                extents.x,
                mins.z,
                maxs.z,
                &mut trace,
            );
        }
        trace
    }

    pub fn probe_geometry(&self, origin: [f32; 3], eye_yaw_deg: f32, ducked: bool) -> GeoProbes {
        let mut out = GeoProbes::default();
        let org = Vec3::new(origin[0], origin[1], origin[2]);
        let hull_max_z = if ducked { 62.0 } else { 82.0 };
        let mins = Vec3::new(-24.0, -24.0, 0.0);
        let maxs = Vec3::new(24.0, 24.0, hull_max_z);

        let g_start = Vec3::new(org.x, org.y, org.z + 2.0);
        let g_end = Vec3::new(org.x, org.y, org.z - 512.0);
        let g_tr = self.trace_hull(g_start, g_end, mins, maxs);
        if g_tr.start_solid {
            out.ground_dist = 0.0;
            out.ground_normal = [0.0, 0.0, 1.0];
        } else if g_tr.hit {
            let hit_z = g_start.z + (g_end.z - g_start.z) * g_tr.fraction;
            out.ground_dist = (org.z - hit_z).max(0.0);
            out.ground_normal = [g_tr.normal.x, g_tr.normal.y, g_tr.normal.z];
        } else {
            out.ground_dist = 512.0;
            out.ground_normal = [0.0, 0.0, 0.0];
        }

        let c_start = Vec3::new(org.x, org.y, org.z);
        let c_end = Vec3::new(org.x, org.y, org.z + 512.0);
        let c_tr = self.trace_hull(c_start, c_end, mins, maxs);
        if c_tr.start_solid {
            out.ceiling_dist = 0.0;
        } else if c_tr.hit {
            out.ceiling_dist = (512.0 * c_tr.fraction).max(0.0);
        } else {
            out.ceiling_dist = 512.0;
        }

        let waist = Vec3::new(org.x, org.y, org.z + 36.0);
        let yaw_rad = eye_yaw_deg.to_radians();
        let mut min_wall = 256.0f32;
        for k in 0..8 {
            let ang = yaw_rad + (k as f32) * std::f32::consts::FRAC_PI_4;
            let dx = ang.cos();
            let dy = ang.sin();
            let w_end = Vec3::new(waist.x + dx * 256.0, waist.y + dy * 256.0, waist.z);
            let w_tr = self.trace_ray(waist, w_end);
            let w_dist = if w_tr.start_solid {
                0.0
            } else if w_tr.hit {
                w_tr.fraction * 256.0
            } else {
                256.0
            };
            out.wall_dists[k] = w_dist;
            if w_dist < min_wall {
                min_wall = w_dist;
            }

            if w_dist < 64.0 {
                out.ledge_depths[k] = -1.0;
            } else {
                let l_start = Vec3::new(org.x + dx * 64.0, org.y + dy * 64.0, org.z + 36.0);
                let l_end = Vec3::new(l_start.x, l_start.y, org.z - 512.0);
                let l_tr = self.trace_ray(l_start, l_end);
                if l_tr.start_solid {
                    out.ledge_depths[k] = -36.0;
                } else if l_tr.hit {
                    let hit_z = l_start.z + (l_end.z - l_start.z) * l_tr.fraction;
                    out.ledge_depths[k] = org.z - hit_z;
                } else {
                    out.ledge_depths[k] = 512.0;
                }
            }
        }
        out.wall_contact = if min_wall < 26.5 { 1 } else { 0 };
        out
    }
}

#[inline]
fn point_in_tri_xy(px: f32, py: f32, a: Vec3, b: Vec3, c: Vec3) -> bool {
    let s1 = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
    let s2 = (c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.y);
    let s3 = (a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x);
    let has_neg = s1 < -0.01 || s2 < -0.01 || s3 < -0.01;
    let has_pos = s1 > 0.01 || s2 > 0.01 || s3 > 0.01;
    !(has_neg && has_pos)
}
