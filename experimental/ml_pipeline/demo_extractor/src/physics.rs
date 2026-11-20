pub const SV_ACCELERATE: f32 = 10.0;
pub const SV_AIRACCELERATE: f32 = 10.0;
pub const SV_FRICTION: f32 = 4.0;
pub const SV_STOPSPEED: f32 = 100.0;
pub const AIR_SPEED_CAP: f32 = 30.0;
pub const TICK_INTERVAL: f32 = 0.015;
pub const DUCK_SPEED_SCALE: f32 = 1.0 / 3.0;

pub fn class_default_maxspeed(class_id: u8) -> f32 {
    match class_id {
        1 => 400.0,
        2 => 300.0,
        3 => 240.0,
        4 => 280.0,
        5 => 320.0,
        6 => 230.0,
        7 => 300.0,
        8 => 320.0,
        9 => 300.0,
        _ => 300.0,
    }
}

#[inline]
pub fn yaw_basis(yaw_deg: f32) -> ([f32; 2], [f32; 2]) {
    let rad = yaw_deg.to_radians();
    let c = rad.cos();
    let s = rad.sin();
    ([c, s], [s, -c])
}

#[inline]
pub fn apply_friction(vx: f32, vy: f32, dt: f32, surface_friction: f32) -> [f32; 2] {
    let speed = (vx * vx + vy * vy).sqrt();
    if speed < 0.1 {
        return [vx, vy];
    }
    let control = speed.max(SV_STOPSPEED);
    let drop = control * SV_FRICTION * surface_friction * dt;
    let newspeed = (speed - drop).max(0.0);
    if newspeed != speed {
        let scale = newspeed / speed;
        [vx * scale, vy * scale]
    } else {
        [vx, vy]
    }
}

#[inline]
pub fn step_ground(
    v0: [f32; 2],
    wish_dir: [f32; 2],
    wish_speed: f32,
    max_speed: f32,
    dt: f32,
    surface_friction: f32,
    lost_footing: bool,
) -> [f32; 2] {
    let mut v = apply_friction(v0[0], v0[1], dt, surface_friction);
    if wish_speed > 0.0 {
        let cur_speed = v[0] * wish_dir[0] + v[1] * wish_dir[1];
        let add_speed = wish_speed - cur_speed;
        if add_speed > 0.0 {
            let mut accel = SV_ACCELERATE;
            let eff_fric = SV_FRICTION * surface_friction;
            let threshold = 100.0 * eff_fric / SV_ACCELERATE;
            if wish_speed < threshold && !lost_footing {
                let sp = (v[0] * v[0] + v[1] * v[1]).sqrt();
                let ctrl = sp.max(SV_STOPSPEED);
                accel = (ctrl * eff_fric) / wish_speed + 1.0;
            }
            let accel_speed = (accel * wish_speed * dt * surface_friction).min(add_speed);
            v[0] += wish_dir[0] * accel_speed;
            v[1] += wish_dir[1] * accel_speed;
        }
    }
    let sp = (v[0] * v[0] + v[1] * v[1]).sqrt();
    if max_speed > 0.0 && sp > max_speed {
        let s = max_speed / sp;
        v[0] *= s;
        v[1] *= s;
    }
    v
}

#[inline]
pub fn step_air(
    v0: [f32; 2],
    wish_dir: [f32; 2],
    wish_speed: f32,
    dt: f32,
    surface_friction: f32,
) -> [f32; 2] {
    if wish_speed <= 0.0 {
        return v0;
    }
    let wish_spd = wish_speed.min(AIR_SPEED_CAP);
    let cur_speed = v0[0] * wish_dir[0] + v0[1] * wish_dir[1];
    let add_speed = wish_spd - cur_speed;
    if add_speed <= 0.0 {
        return v0;
    }
    let accel_speed = (SV_AIRACCELERATE * wish_speed * dt * surface_friction).min(add_speed);
    [v0[0] + wish_dir[0] * accel_speed, v0[1] + wish_dir[1] * accel_speed]
}

pub fn infer_wish_dir(
    v0: [f32; 2],
    v1: [f32; 2],
    eye_yaw_deg: f32,
    on_ground: bool,
    ducking: bool,
    max_speed_raw: f32,
    class_id: u8,
    n_ticks: u32,
    lost_footing: bool,
) -> (f32, f32) {
    let n_sub = n_ticks.clamp(1, 4);
    let dt = TICK_INTERVAL;
    let base_max = if max_speed_raw > 10.0 {
        max_speed_raw
    } else {
        class_default_maxspeed(class_id)
    };
    let eff_max = if on_ground && ducking {
        base_max * DUCK_SPEED_SCALE
    } else {
        base_max
    };

    let (fwd_vec, right_vec) = yaw_basis(eye_yaw_deg);

    let mut v_idle = v0;
    if on_ground {
        for _ in 0..n_sub {
            v_idle = apply_friction(v_idle[0], v_idle[1], dt, 1.0);
        }
    }
    let dv = [v1[0] - v_idle[0], v1[1] - v_idle[1]];
    let dv_mag = (dv[0] * dv[0] + dv[1] * dv[1]).sqrt();

    let candidates: [(f32, f32); 9] = [
        (0.0, 0.0),
        (1.0, 0.0),
        (-1.0, 0.0),
        (0.0, 1.0),
        (0.0, -1.0),
        (1.0, 1.0),
        (1.0, -1.0),
        (-1.0, 1.0),
        (-1.0, -1.0),
    ];

    let mut best_idx = 0usize;
    let mut best_err2 = f32::MAX;
    let mut zero_err2 = f32::MAX;

    for (idx, &(wf, ws)) in candidates.iter().enumerate() {
        let wx = fwd_vec[0] * wf + right_vec[0] * ws;
        let wy = fwd_vec[1] * wf + right_vec[1] * ws;
        let wlen = (wx * wx + wy * wy).sqrt();
        let (wish_dir, wish_speed) = if wlen > 1e-6 {
            ([wx / wlen, wy / wlen], eff_max)
        } else {
            ([0.0, 0.0], 0.0)
        };

        let mut v_sim = v0;
        for _ in 0..n_sub {
            v_sim = if on_ground {
                step_ground(v_sim, wish_dir, wish_speed, eff_max, dt, 1.0, lost_footing)
            } else {
                step_air(v_sim, wish_dir, wish_speed, dt, 1.0)
            };
        }
        let ex = v_sim[0] - v1[0];
        let ey = v_sim[1] - v1[1];
        let err2 = ex * ex + ey * ey;
        if idx == 0 {
            zero_err2 = err2;
        }
        if err2 < best_err2 {
            best_err2 = err2;
            best_idx = idx;
        }
    }

    if best_idx == 0 {
        let v1_mag = (v1[0] * v1[0] + v1[1] * v1[1]).sqrt();
        if on_ground && v1_mag > eff_max * 0.92 && (v1_mag - (v0[0] * v0[0] + v0[1] * v0[1]).sqrt()).abs() < 2.0 {
            let dir_x = v1[0] / v1_mag;
            let dir_y = v1[1] / v1_mag;
            let pf = dir_x * fwd_vec[0] + dir_y * fwd_vec[1];
            let ps = dir_x * right_vec[0] + dir_y * right_vec[1];
            return (pf.clamp(-1.0, 1.0), ps.clamp(-1.0, 1.0));
        }
        return (0.0, 0.0);
    }

    if (zero_err2 - best_err2) < 0.25 && dv_mag < 1.5 {
        return (0.0, 0.0);
    }

    if dv_mag >= 2.0 && dv_mag <= eff_max * SV_ACCELERATE * dt * (n_sub as f32) * 1.1 {
        let cont_dir = [dv[0] / dv_mag, dv[1] / dv_mag];
        let mut v_cont = v0;
        for _ in 0..n_sub {
            v_cont = if on_ground {
                step_ground(v_cont, cont_dir, eff_max, eff_max, dt, 1.0, lost_footing)
            } else {
                step_air(v_cont, cont_dir, eff_max, dt, 1.0)
            };
        }
        let cex = v_cont[0] - v1[0];
        let cey = v_cont[1] - v1[1];
        if cex * cex + cey * cey < best_err2 * 0.6 {
            let pf = cont_dir[0] * fwd_vec[0] + cont_dir[1] * fwd_vec[1];
            let ps = cont_dir[0] * right_vec[0] + cont_dir[1] * right_vec[1];
            let mag = (pf * pf + ps * ps).sqrt().max(1e-6);
            return (pf / mag, ps / mag);
        }
    }

    let (bf, bs) = candidates[best_idx];
    let mag = (bf * bf + bs * bs).sqrt();
    (bf / mag, bs / mag)
}
