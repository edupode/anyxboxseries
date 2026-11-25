//! Tests for Wave64-on-Wave32 and portable LDS Wave64 compute lowering,
//! using synthetic GFX10 machine code on hardware GPU and WARP.

use super::{HostShader, TranslateOptions, Wave64Mode, emit_with, translate_with};
use crate::dxbc::{Container, FourCC};
use crate::native::{ComputeIo, HwStage, NativeProgram, RootParam, RootType, StageIo};
use crate::{ShaderError, io, ir, resource, structure};

const S_ENDPGM: u32 = 0xBF81_0000;
const EXEC_LO: u32 = 126;
const EXEC_HI: u32 = 127;
const VCC: u32 = 106;
const NULL_OP: u32 = 125;

// Inline constants and register operands in 9-bit GFX10 source fields.
fn vgpr(v: u32) -> u32 {
    256 + v
}
fn inline_u(n: u32) -> u32 {
    assert!(n <= 64);
    0x80 + n
}
const NEG_ONE: u32 = 0xC1;
const LIT: u32 = 0xFF;

fn sop1(dst: u32, op: u32, src0: u32) -> u32 {
    0xBE80_0000 | (dst << 16) | (op << 8) | src0
}
fn sop2(dst: u32, op: u32, src0: u32, src1: u32) -> u32 {
    0x8000_0000 | (op << 23) | (dst << 16) | (src1 << 8) | src0
}
fn sopc(op: u32, src0: u32, src1: u32) -> u32 {
    0xBF00_0000 | (op << 16) | (src1 << 8) | src0
}
fn sopp(op: u32, imm: i16) -> u32 {
    0xBF80_0000 | (op << 16) | u32::from(imm as u16)
}
fn vop1(dst: u32, op: u32, src0: u32) -> u32 {
    0x7E00_0000 | (dst << 17) | (op << 9) | src0
}
fn vop2(op: u32, dst: u32, src0: u32, vsrc1: u32) -> u32 {
    (op << 25) | (dst << 17) | (vsrc1 << 9) | src0
}
fn vopc(op: u32, src0: u32, vsrc1: u32) -> u32 {
    0x7C00_0000 | (op << 17) | (vsrc1 << 9) | src0
}
fn vop3(op: u32, dst: u32, src0: u32, src1: u32, src2: u32) -> [u32; 2] {
    [
        0xD400_0000 | (op << 16) | dst,
        src0 | (src1 << 9) | (src2 << 18),
    ]
}
fn ds_op(op: u32, off0: u8, off1: u8, addr: u32, data0: u32, vdst: u32) -> [u32; 2] {
    [
        0xD800_0000 | (op << 18) | (u32::from(off1) << 8) | u32::from(off0),
        addr | (data0 << 8) | (vdst << 24),
    ]
}
fn dpp_row_shr(
    vop2_op: u32,
    dst: u32,
    vsrc1: u32,
    vsrc0: u32,
    ctrl: u32,
    row_mask: u32,
) -> [u32; 2] {
    [
        vop2(vop2_op, dst, 0xFA, vsrc1),
        vsrc0 | (ctrl << 8) | (1 << 19) | (0xF << 24) | (row_mask << 28),
    ]
}

fn s_mov_b32(dst: u32, src0: u32) -> u32 {
    sop1(dst, 0x03, src0)
}
fn s_mov_b64(dst: u32, src0: u32) -> u32 {
    sop1(dst, 0x04, src0)
}
fn s_bcnt1_i32_b64(dst: u32, src0: u32) -> u32 {
    sop1(dst, 0x10, src0)
}
fn s_ff1_i32_b64(dst: u32, src0: u32) -> u32 {
    sop1(dst, 0x14, src0)
}
fn s_and_saveexec_b64(dst: u32, src0: u32) -> u32 {
    sop1(dst, 0x24, src0)
}
fn s_or_saveexec_b64(dst: u32, src0: u32) -> u32 {
    sop1(dst, 0x25, src0)
}
fn s_add_i32(dst: u32, src0: u32, src1: u32) -> u32 {
    sop2(dst, 0x02, src0, src1)
}
fn s_or_b64(dst: u32, src0: u32, src1: u32) -> u32 {
    sop2(dst, 0x11, src0, src1)
}
fn s_cmp_lt_u32(src0: u32, src1: u32) -> u32 {
    sopc(0x0A, src0, src1)
}
fn s_cbranch_scc1(delta_words: i16) -> u32 {
    sopp(0x05, delta_words)
}
fn s_cbranch_execz(delta_words: i16) -> u32 {
    sopp(0x08, delta_words)
}

fn v_mov_b32(dst: u32, src0: u32) -> u32 {
    vop1(dst, 0x01, src0)
}
fn v_readfirstlane_b32(sdst: u32, vsrc: u32) -> u32 {
    vop1(sdst, 0x02, vgpr(vsrc))
}
fn v_lshlrev_b32(dst: u32, shift: u32, vsrc1: u32) -> u32 {
    vop2(0x1A, dst, shift, vsrc1)
}
fn v_and_b32(dst: u32, src0: u32, vsrc1: u32) -> u32 {
    vop2(0x1B, dst, src0, vsrc1)
}
fn v_or_b32(dst: u32, src0: u32, vsrc1: u32) -> u32 {
    vop2(0x1C, dst, src0, vsrc1)
}
fn v_add_nc_u32(dst: u32, src0: u32, vsrc1: u32) -> u32 {
    vop2(0x25, dst, src0, vsrc1)
}
fn v_sub_nc_u32(dst: u32, src0: u32, vsrc1: u32) -> u32 {
    vop2(0x26, dst, src0, vsrc1)
}
fn v_cmp_lt_u32(src0: u32, vsrc1: u32) -> u32 {
    vopc(0xC1, src0, vsrc1)
}
fn v_cmp_eq_u32(src0: u32, vsrc1: u32) -> u32 {
    vopc(0xC2, src0, vsrc1)
}
fn v_cmp_gt_u32(src0: u32, vsrc1: u32) -> u32 {
    vopc(0xC4, src0, vsrc1)
}
fn v_cndmask_b32(dst: u32, src0: u32, src1: u32, mask_s: u32) -> [u32; 2] {
    vop3(0x101, dst, src0, src1, mask_s)
}
fn v_readlane_b32(sdst: u32, vsrc: u32, lane_op: u32) -> [u32; 2] {
    vop3(0x360, sdst, vgpr(vsrc), lane_op, 0)
}
fn v_writelane_b32(vdst: u32, src: u32, lane_op: u32) -> [u32; 2] {
    vop3(0x361, vdst, src, lane_op, 0)
}
fn v_mbcnt_lo_u32_b32(dst: u32, mask: u32, b: u32) -> [u32; 2] {
    vop3(0x365, dst, mask, b, 0)
}
fn v_mbcnt_hi_u32_b32(dst: u32, mask: u32, b: u32) -> [u32; 2] {
    vop3(0x366, dst, mask, b, 0)
}

fn s_load_dwordx4(sdst: u32, sbase: u32) -> [u32; 2] {
    [0xF408_0000 | (sdst << 6) | (sbase >> 1), NULL_OP << 25]
}

fn buffer_store_dword(vdata: u32, vaddr: u32, srsrc: u32) -> [u32; 2] {
    [
        0xE070_1000, // buffer_store_dword, offen=1
        vaddr | (vdata << 8) | ((srsrc >> 2) << 16) | (inline_u(0) << 24),
    ]
}

fn emit_uav_store(code: &mut Vec<u32>, data_vreg: u32, tid_vreg: u32) {
    // s0 holds heap base 0 (user param 0 = ResourceHeap); load view 0 into s[8:11].
    code.push(s_mov_b32(1, inline_u(0)));
    code.extend_from_slice(&s_load_dwordx4(8, 0));
    code.push(v_lshlrev_b32(15, inline_u(2), tid_vreg));
    code.extend_from_slice(&buffer_store_dword(data_vreg, 15, 8));
}

fn empty_dxbc() -> Vec<u8> {
    let mut b = Vec::with_capacity(32);
    b.extend_from_slice(b"DXBC");
    b.extend_from_slice(&[0u8; 16]);
    b.extend_from_slice(&1u32.to_le_bytes()); // major/minor
    b.extend_from_slice(&32u32.to_le_bytes());
    b.extend_from_slice(&0u32.to_le_bytes());
    b
}

fn make_cs_program(
    words: &[u32],
    threads: [u32; 3],
    lds_bytes: u32,
    group_id_x: bool,
) -> NativeProgram {
    let lds_granules = lds_bytes.div_ceil(512);
    // 2 user SGPRs (s[0:1] heap pointer); optional TGID.x in bit 7.
    let rsrc2 = (2 << 1) | (u32::from(group_id_x) << 7) | (lds_granules << 15);
    NativeProgram {
        stage: HwStage::Cs,
        wave64: true,
        vgprs: 32,
        layout: vec![RootParam {
            root: RootType::ResourceHeap,
            table_offset_dwords: 0,
            sgprs: 1,
            base_register: 0,
            user_data: 0,
            slot: 0,
        }],
        used: Vec::new(),
        io: StageIo::Cs(ComputeIo { rsrc2, threads }),
        code: words.iter().flat_map(|w| w.to_le_bytes()).collect(),
    }
}

fn emit_cs(native: &NativeProgram, mode: Wave64Mode) -> Result<HostShader, ShaderError> {
    let dxbc = empty_dxbc();
    let container = Container::parse(&dxbc)?;
    let p = ir::build(&native.patched_code(), native.wave64, native.vgprs)?;
    let res = resource::track(&p, &native.layout, native.stage)?;
    let io_res = io::raise(&p, native)?;
    let st = structure::structure(&p)?;
    emit_with(
        &container,
        native,
        &p,
        &st,
        &io_res,
        &res,
        TranslateOptions { wave64: mode },
    )
}

fn psv_wave_range(shader: &HostShader) -> (u32, u32) {
    let c = Container::parse(&shader.dxil).unwrap();
    let psv = c.part(FourCC::PSV0).unwrap().data;
    let min = u32::from_le_bytes(psv[20..24].try_into().unwrap());
    let max = u32::from_le_bytes(psv[24..28].try_into().unwrap());
    (min, max)
}

fn sfi0_flags(shader: &HostShader) -> u64 {
    let c = Container::parse(&shader.dxil).unwrap();
    match c.part(FourCC::SFI0) {
        Some(p) => u64::from_le_bytes(p.data[0..8].try_into().unwrap()),
        None => 0,
    }
}

#[test]
fn wave_mode_psv_and_flags() {
    // 128 threads reading 64-lane ballot + mbcnt.
    let mut code = Vec::new();
    code.push(v_cmp_lt_u32(inline_u(40), 0)); // vcc = (40 < v0) i.e. tid & 63 >= 41
    code.push(s_bcnt1_i32_b64(4, VCC));
    code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
    code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
    code.push(v_add_nc_u32(1, 4, 1));
    emit_uav_store(&mut code, 1, 0);
    code.push(S_ENDPGM);

    let prog = make_cs_program(&code, [128, 1, 1], 0, false);

    let native = emit_cs(&prog, Wave64Mode::Native).unwrap();
    assert_eq!(psv_wave_range(&native), (64, 64));
    assert_ne!(sfi0_flags(&native) & 0x4000, 0);

    let wave32 = emit_cs(&prog, Wave64Mode::Wave32).unwrap();
    assert_eq!(psv_wave_range(&wave32), (32, 32));

    let emu = emit_cs(&prog, Wave64Mode::Emulate).unwrap();
    assert_eq!(psv_wave_range(&emu), (0, u32::MAX));
    assert_eq!(sfi0_flags(&emu) & 0x4000, 0);
}

#[test]
fn lds_overflow_fails_explicitly() {
    // Title uses full 32 KiB (8192 dwords) of LDS and reads a dynamic lane.
    let mut code = Vec::new();
    code.extend_from_slice(&v_readlane_b32(4, 0, vgpr(0)));
    code.push(v_mov_b32(1, 4));
    emit_uav_store(&mut code, 1, 0);
    code.push(S_ENDPGM);

    let prog = make_cs_program(&code, [1024, 1, 1], 32768, false);
    assert!(emit_cs(&prog, Wave64Mode::Native).is_ok());

    for mode in [Wave64Mode::Wave32, Wave64Mode::Emulate] {
        let err = emit_cs(&prog, mode).unwrap_err();
        assert!(
            matches!(err, ShaderError::Unsupported { what, .. } if what.contains("32 KiB")),
            "expected explicit LDS overflow error in {mode:?}, got {err:?}"
        );
    }
}

#[test]
fn divergent_cross_half_ballot_is_rejected_instead_of_hanging() {
    // if (v0 < 32) { s4 = bcnt1(v0 < 10); v1 = s4; } else { v1 = 0; } store(v1)
    let mut code = Vec::new();
    code.push(v_mov_b32(1, inline_u(0)));
    code.push(v_cmp_gt_u32(inline_u(32), 0)); // 32 > v0
    code.push(s_and_saveexec_b64(6, VCC)); // s[6:7]
    let br_idx = code.len();
    code.push(0); // placeholder for s_cbranch_execz -> after_then
    code.push(v_cmp_gt_u32(inline_u(10), 0)); // 10 > v0
    code.push(s_bcnt1_i32_b64(4, VCC));
    code.push(v_mov_b32(1, 4));
    let after_then = code.len();
    code[br_idx] = s_cbranch_execz((after_then - (br_idx + 1)) as i16);
    code.push(s_or_b64(EXEC_LO, EXEC_LO, 6));
    emit_uav_store(&mut code, 1, 0);
    code.push(S_ENDPGM);

    let prog = make_cs_program(&code, [128, 1, 1], 0, false);
    assert!(emit_cs(&prog, Wave64Mode::Native).is_ok());
    for mode in [Wave64Mode::Wave32, Wave64Mode::Emulate] {
        let err = emit_cs(&prog, mode).unwrap_err();
        assert!(
            matches!(err, ShaderError::Unsupported { .. }),
            "expected safe error for divergent ballot in {mode:?}, got {err:?}"
        );
    }
}

#[test]
fn divergent_early_return_taints_subsequent_blocks() {
    // if (v0 < 32) return; // after_ret is non-uniform across the group!
    // bcnt1 in after_ret must fail instead of inserting a hanging barrier.
    let mut code = Vec::new();
    code.push(v_cmp_gt_u32(inline_u(32), 0));
    code.push(s_and_saveexec_b64(6, VCC));
    let br_idx = code.len();
    code.push(0);
    code.push(S_ENDPGM);
    let rest = code.len();
    code[br_idx] = s_cbranch_execz((rest - (br_idx + 1)) as i16);
    code.push(v_cmp_gt_u32(LIT, 0));
    code.push(96);
    code.push(s_bcnt1_i32_b64(4, VCC));
    code.push(v_mov_b32(1, 4));
    emit_uav_store(&mut code, 1, 0);
    code.push(S_ENDPGM);

    let prog = make_cs_program(&code, [128, 1, 1], 0, false);
    assert!(emit_cs(&prog, Wave64Mode::Native).is_ok());
    for mode in [Wave64Mode::Wave32, Wave64Mode::Emulate] {
        assert!(matches!(
            emit_cs(&prog, mode),
            Err(ShaderError::Unsupported { .. })
        ));
    }
}

#[test]
fn translate_with_handles_synthetic_wave64_container() {
    // Build genuine DXBC + XDAT(XBCH(FSSM)) Wave64 compute container with ResourceHeap param.
    let mut code = Vec::new();
    code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
    code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
    emit_uav_store(&mut code, 1, 0);
    code.push(S_ENDPGM);
    let code_bytes: Vec<u8> = code.iter().flat_map(|w| w.to_le_bytes()).collect();

    const ABSENT: u64 = 0xDEAD_BEEF_DEAD_BEEF;
    const RECORD_SIZE: usize = 0x54;
    const STAGE_SIZE: usize = 0x34;
    const ENTRY_SIZE: usize = 20;

    let mut payload = 6u64.to_le_bytes().to_vec(); // GRAPHICS_SLOT_COUNT
    payload.resize(0x68, 0);
    for slot_off in [0x08usize, 0x10, 0x18, 0x20, 0x28, 0x30, 0x48, 0x60] {
        payload[slot_off..slot_off + 8].copy_from_slice(&ABSENT.to_le_bytes());
    }
    let rec = payload.len() as u64;
    payload[0x60..0x68].copy_from_slice(&rec.to_le_bytes());
    payload.resize(rec as usize + RECORD_SIZE + STAGE_SIZE, 0);

    let r = rec as usize;
    let code_at = (rec as usize + RECORD_SIZE + STAGE_SIZE) as u64;
    let layout_at = code_at + code_bytes.len() as u64;
    payload[r..r + 8].copy_from_slice(&code_at.to_le_bytes());
    payload[r + 8..r + 16].copy_from_slice(&(code_bytes.len() as u64).to_le_bytes());
    payload[r + 0x28..r + 0x30].copy_from_slice(&layout_at.to_le_bytes()); // RECORD_LAYOUT
    payload[r + 0x30..r + 0x38].copy_from_slice(&layout_at.to_le_bytes()); // RECORD_USED
    payload[r + 0x38..r + 0x3C].copy_from_slice(&1u32.to_le_bytes()); // 1 root param
    payload[r + 0x3C..r + 0x40].copy_from_slice(&0u32.to_le_bytes()); // 0 used entries
    payload[r + 0x50..r + 0x54].copy_from_slice(&(32u32 | (31 << 20)).to_le_bytes()); // RECORD_REGS

    let sd = r + RECORD_SIZE;
    payload[sd + 0x24..sd + 0x28].copy_from_slice(&(2u32 << 1).to_le_bytes()); // CS_RSRC2: 2 user SGPRs
    payload[sd + 0x28..sd + 0x2C].copy_from_slice(&64u32.to_le_bytes()); // CS_THREADS[0]
    payload[sd + 0x2C..sd + 0x30].copy_from_slice(&1u32.to_le_bytes());
    payload[sd + 0x30..sd + 0x34].copy_from_slice(&1u32.to_le_bytes());

    payload.extend_from_slice(&code_bytes);
    // RootParam: ResourceHeap (5), table_off=0, sgprs=1, base_reg=0, user_data=0, slot=0
    let mut entry = [0u8; ENTRY_SIZE];
    entry[0..4].copy_from_slice(&5u32.to_le_bytes());
    entry[8..12].copy_from_slice(&1u32.to_le_bytes());
    payload.extend_from_slice(&entry);

    let mut fssm = b"FSSM".to_vec();
    fssm.extend_from_slice(&0x101u32.to_le_bytes()); // FSSM_VERSION
    fssm.extend_from_slice(&(payload.len() as u64).to_le_bytes());
    fssm.extend_from_slice(&payload);

    let compressed = miniz_oxide::deflate::compress_to_vec_zlib(&fssm, 6);
    let mut xbch = b"XBCH".to_vec();
    xbch.extend_from_slice(&3u32.to_le_bytes()); // XBCH_VERSION
    xbch.extend_from_slice(&(compressed.len() as u64).to_le_bytes());
    xbch.extend_from_slice(&(fssm.len() as u64).to_le_bytes());
    xbch.extend_from_slice(&compressed);

    let mut dxil_part = vec![0u8; 0x18];
    dxil_part.extend_from_slice(b"XDAT");
    dxil_part.extend_from_slice(&1u32.to_le_bytes());
    dxil_part.extend_from_slice(&0x24u32.to_le_bytes());
    let blob_off = (dxil_part.len() + 12) as u32;
    for v in [0x0Du32, blob_off, xbch.len() as u32] {
        dxil_part.extend_from_slice(&v.to_le_bytes());
    }
    dxil_part.extend_from_slice(&xbch);

    let part_off = 0x24u32;
    let total = part_off as usize + 8 + dxil_part.len();
    let mut dxbc = b"DXBC".to_vec();
    dxbc.extend_from_slice(&[0u8; 16]);
    dxbc.extend_from_slice(&1u32.to_le_bytes());
    dxbc.extend_from_slice(&(total as u32).to_le_bytes());
    dxbc.extend_from_slice(&1u32.to_le_bytes());
    dxbc.extend_from_slice(&part_off.to_le_bytes());
    dxbc.extend_from_slice(b"DXIL");
    dxbc.extend_from_slice(&(dxil_part.len() as u32).to_le_bytes());
    dxbc.extend_from_slice(&dxil_part);

    let sh_n = translate_with(
        &dxbc,
        TranslateOptions {
            wave64: Wave64Mode::Native,
        },
    )
    .unwrap();
    assert_eq!(psv_wave_range(&sh_n), (64, 64));
    let sh_w32 = translate_with(
        &dxbc,
        TranslateOptions {
            wave64: Wave64Mode::Wave32,
        },
    )
    .unwrap();
    assert_eq!(psv_wave_range(&sh_w32), (32, 32));
    let sh_emu = translate_with(
        &dxbc,
        TranslateOptions {
            wave64: Wave64Mode::Emulate,
        },
    )
    .unwrap();
    assert_eq!(psv_wave_range(&sh_emu), (0, u32::MAX));
}

#[cfg(windows)]
mod gpu {
    use std::sync::{Mutex, OnceLock};

    use super::*;
    use crate::emit::host::{Dispatch, Draw, Host, View};

    fn with_hosts(f: impl FnOnce(&[Option<Host>; 2])) {
        static HOSTS: OnceLock<Mutex<[Option<Host>; 2]>> = OnceLock::new();
        let lock = HOSTS.get_or_init(|| Mutex::new([Host::new(false).ok(), Host::new(true).ok()]));
        let guard = lock.lock().unwrap_or_else(|e| e.into_inner());
        f(&guard);
    }

    fn run_on_hosts(
        name: &str,
        prog: &NativeProgram,
        groups: [u32; 3],
        uav_dwords: u32,
        expected: &[u32],
        expect_native_fail: bool,
    ) {
        with_hosts(|hosts| {
            for (warp_idx, host) in hosts.iter().enumerate() {
                let Some(host) = host else {
                    continue;
                };
                let warp = warp_idx == 1;
                let mut modes = vec![Wave64Mode::Emulate];
                if host.supports_wave32() {
                    modes.push(Wave64Mode::Wave32);
                }
                if expect_native_fail && !host.supports_wave64() {
                    let native = emit_cs(prog, Wave64Mode::Native).unwrap();
                    assert!(
                        host.compute_pipeline(&native).is_err(),
                        "{name}: Native Wave64 unexpectedly built a PSO on warp={warp}"
                    );
                }
                for mode in modes {
                    let shader = emit_cs(prog, mode)
                        .unwrap_or_else(|e| panic!("{name} ({mode:?}, warp={warp}): {e}"));
                    if let Ok(mut v) = super::super::validator::Validator::load(
                        super::super::validator::Validator::default_path().as_deref(),
                    ) {
                        v.validate(&mut shader.dxil.clone())
                            .unwrap_or_else(|e| panic!("{name} ({mode:?}) validate: {e}"));
                    }
                    let (root, pso) = host
                        .compute_pipeline(&shader)
                        .unwrap_or_else(|e| panic!("{name} ({mode:?}, warp={warp}) PSO: {e}"));
                    let out = host
                        .dispatch(
                            &root,
                            &pso,
                            &Dispatch {
                                roots: Vec::new(),
                                views: &[View::RawUav(uav_dwords)],
                                samplers: &[],
                                groups,
                            },
                        )
                        .unwrap_or_else(|e| panic!("{name} ({mode:?}, warp={warp}) dispatch: {e}"));
                    assert_eq!(
                        &out[0][..expected.len()],
                        expected,
                        "{name} ({mode:?}, warp={warp}) output mismatch"
                    );
                }
            }
        });
    }

    // ---- synthetic mesh programs ----

    fn make_ms_program(
        words: &[u32],
        threads: u32,
        lds_bytes: u32,
        max_vertices: u32,
        max_primitives: u32,
    ) -> NativeProgram {
        NativeProgram {
            stage: HwStage::Ms,
            wave64: true,
            vgprs: 32,
            // Mesh user data starts after the 8 system SGPRs (s8).
            layout: vec![RootParam {
                root: RootType::ResourceHeap,
                table_offset_dwords: 0,
                sgprs: 1,
                base_register: 8,
                user_data: 0,
                slot: 0,
            }],
            used: Vec::new(),
            io: StageIo::Ms(crate::native::MeshIo {
                outputs: Vec::new(),
                clip_cntl: 0,
                threads,
                max_vertices,
                max_primitives,
                topology: 2,
                lds_bytes,
            }),
            code: words.iter().flat_map(|w| w.to_le_bytes()).collect(),
        }
    }

    /// Stores `data` at dword `addr` through the first raw UAV. `addr` and
    /// `data` are VGPR numbers.
    fn emit_ms_uav_store(code: &mut Vec<u32>, data: u32, addr: u32) {
        // Heap base (user-data dword 0) is s8, its high half 0; descriptor 0
        // into the 4-aligned s[12:15].
        code.push(s_mov_b32(9, inline_u(0)));
        code.extend_from_slice(&s_load_dwordx4(12, 8));
        code.push(v_lshlrev_b32(15, inline_u(2), addr));
        code.extend_from_slice(&buffer_store_dword(data, 15, 12));
    }

    /// A container for a synthetic mesh program with one `SV_Position` output
    /// (a mesh shader without outputs is not a valid pipeline).
    fn mesh_dxbc() -> Vec<u8> {
        use crate::emit::container::{self, SigElement};
        let pos = SigElement {
            name: "SV_Position".into(),
            index: 0,
            kind: 3,
            comp: 9,
            interp: 0,
            row: Some(0),
            col: 0,
            cols: 4,
            usage: 0,
            half: false,
        };
        let none_in = container::signature(&[], true);
        let out = container::signature(&[pos], false);
        let prims = container::signature(&[], false);
        container::build(&[
            (b"ISG1", &none_in),
            (b"OSG1", &out),
            (b"PSG1", &prims),
        ])
    }

    /// Writes `SV_Position` (0,0,0,0) from v10-v13 (a mesh shader must write
    /// every output its signature declares).
    fn emit_ms_position(code: &mut Vec<u32>) {
        for v in 10..14 {
            code.push(v_mov_b32(v, inline_u(0)));
        }
        // exp pos0, v10, v11, v12, v13
        code.push(0xF800_0000 | (12 << 4) | 0xF);
        code.push(10 | (11 << 8) | (12 << 16) | (13 << 24));
    }

    fn emit_ms(native: &NativeProgram, mode: Wave64Mode) -> Result<HostShader, ShaderError> {
        let dxbc = mesh_dxbc();
        let container = Container::parse(&dxbc)?;
        let p = ir::build(&native.patched_code(), native.wave64, native.vgprs)?;
        let res = resource::track(&p, &native.layout, native.stage)?;
        let st = structure::structure(&p)?;
        let mut io = io::raise(&p, native)?;
        // A synthetic mesh program reads its thread index from VGPR 0-2
        // (the console derives it from EXEC); map them explicitly.
        for &(reg, v) in &p.inputs {
            let input = match reg {
                crate::ir::Reg::V(0) => io::Input::ThreadIdInGroup(0),
                crate::ir::Reg::V(1) => io::Input::ThreadIdInGroup(1),
                crate::ir::Reg::V(2) => io::Input::ThreadIdInGroup(2),
                _ => continue,
            };
            if !io.inputs.iter().any(|&(x, _)| x == v) {
                io.inputs.push((v, input));
            }
        }
        emit_with(
            &container,
            native,
            &p,
            &st,
            &io,
            &res,
            TranslateOptions { wave64: mode },
        )
    }

    /// A minimal pixel shader for a mesh pipeline, compiled with a `dxc` on
    /// PATH (none: `None`, and mesh drawing tests are skipped).
    fn compile_ps(source: &str) -> Option<Vec<u8>> {
        use std::sync::atomic::{AtomicUsize, Ordering};
        static N: AtomicUsize = AtomicUsize::new(0);
        let dxc = std::env::var_os("VICTORY_PC_DXC").unwrap_or_else(|| "dxc".into());
        let dir = std::env::temp_dir().join(format!(
            "victory-ms-ps-{}-{}",
            std::process::id(),
            N.fetch_add(1, Ordering::Relaxed)
        ));
        std::fs::create_dir_all(&dir).ok()?;
        let src = dir.join("mps.hlsl");
        let out = dir.join("mps.dxil");
        std::fs::write(&src, source).ok()?;
        let status = std::process::Command::new(&dxc)
            .args(["-T", "ps_6_6", "-E", "main", "-Fo"])
            .arg(&out)
            .arg(&src)
            .status()
            .ok()?;
        if !status.success() {
            return None;
        }
        std::fs::read(&out).ok()
    }

    fn trivial_ps() -> Option<Vec<u8>> {
        compile_ps("float4 main() : SV_Target { return 0; }\n")
    }

    fn red_ps() -> Option<Vec<u8>> {
        compile_ps("float4 main() : SV_Target { return float4(1, 0, 0, 1); }\n")
    }

    /// Emits and validates `prog`, then creates the mesh pipeline a
    /// `DispatchMesh` needs (with a trivial pixel shader).
    fn mesh_pso(
        name: &str,
        prog: &NativeProgram,
    ) -> Option<(
        Host,
        windows::Win32::Graphics::Direct3D12::ID3D12RootSignature,
        windows::Win32::Graphics::Direct3D12::ID3D12PipelineState,
        HostShader,
    )> {
        mesh_pso_with_ps(name, prog, trivial_ps())
    }

    /// [`mesh_pso`] with the pixel shader the drawing test needs.
    fn mesh_pso_with_ps(
        name: &str,
        prog: &NativeProgram,
        ps: Option<Vec<u8>>,
    ) -> Option<(
        Host,
        windows::Win32::Graphics::Direct3D12::ID3D12RootSignature,
        windows::Win32::Graphics::Direct3D12::ID3D12PipelineState,
        HostShader,
    )> {
        let shader = emit_ms(prog, Wave64Mode::Native)
            .unwrap_or_else(|e| panic!("{name}: {e}"));
        if let Ok(mut v) = super::super::validator::Validator::load(
            super::super::validator::Validator::default_path().as_deref(),
        ) {
            v.validate(&mut shader.dxil.clone())
                .unwrap_or_else(|e| panic!("{name}: validate: {e}"));
        }
        let Some(ps) = ps else {
            eprintln!("{name}: no dxc for a pixel shader; PSO skipped");
            return None;
        };
        let Some(host) = Host::new(false).ok() else {
            eprintln!("{name}: no SM 6.6 device; skipped");
            return None;
        };
        let ps_shader = HostShader {
            stage: HwStage::Ps,
            dxil: ps,
            user_data: Vec::new(),
            samplers: Vec::new(),
            static_samplers: Vec::new(),
            root_cbvs: Vec::new(),
            root_srvs: Vec::new(),
        };
        let root = host
            .root_signature_for(&[
                (&shader, windows::Win32::Graphics::Direct3D12::D3D12_SHADER_VISIBILITY_MESH),
                (&ps_shader, windows::Win32::Graphics::Direct3D12::D3D12_SHADER_VISIBILITY_PIXEL),
            ])
            .unwrap_or_else(|e| panic!("{name}: root signature: {e}"));
        let pso = host
            .mesh_pipeline_with(&root, &shader.dxil, &ps_shader.dxil)
            .unwrap_or_else(|e| panic!("{name}: PSO: {e}"));
        Some((host, root, pso, shader))
    }

    fn run_mesh(name: &str, prog: &NativeProgram, groups: u32, uav_dwords: u32, expected: &[u32]) {
        let Some((host, root, pso, _)) = mesh_pso(name, prog) else {
            return;
        };
        let out = host
            .dispatch_mesh(
                &root,
                &pso,
                &Dispatch {
                    roots: Vec::new(),
                    views: &[View::RawUav(uav_dwords)],
                    samplers: &[],
                    groups: [1, 1, 1],
                },
                groups,
            )
            .unwrap_or_else(|e| panic!("{name}: dispatch: {e}"));
        assert_eq!(&out[0][..expected.len()], expected, "{name}: output mismatch");
    }

    // ---- allocation counts (`s_sendmsg GS_ALLOC_REQ`, `m0`) ----

    const S_SENDMSG_ALLOC_REQ: u32 = 0xBF90_0009;

    fn s_movk_i32(dst: u32, imm: u16) -> u32 {
        0xB000_0000 | (dst << 16) | u32::from(imm)
    }
    fn s_cmp_eq_u32(src0: u32, src1: u32) -> u32 {
        sopc(0x06, src0, src1)
    }
    fn s_branch(delta_words: i16) -> u32 {
        sopp(0x02, delta_words)
    }
    fn s_nop() -> u32 {
        sopp(0x00, 0)
    }
    fn v_literal(dst: u32, bits: u32) -> [u32; 2] {
        [v_mov_b32(dst, LIT), bits]
    }

    /// A 128-thread mesh program that requests `packed` (vertices, then
    /// primitives << 12) and takes one of two paths into a join: the
    /// request's immediate postdominator, where the host declares the
    /// counts. The join is at word 7.
    fn ms_alloc_counts_head(packed: u16) -> Vec<u32> {
        vec![
            s_movk_i32(124, packed), // m0
            S_SENDMSG_ALLOC_REQ,
            s_mov_b32(30, inline_u(0)),
            s_cmp_eq_u32(30, inline_u(0)),
            s_cbranch_scc1(2), // -> join
            s_branch(1),       // -> join
            s_nop(),           // the other path
        ]
    }

    /// Every thread exports the same full-screen triangle (vertices
    /// 0, 1, 2 and indices (0,1,2)); only the declared counts are used.
    fn emit_ms_screen_triangle(code: &mut Vec<u32>) {
        code.extend_from_slice(&v_literal(14, (-1.0f32).to_bits()));
        code.extend_from_slice(&v_literal(15, 3.0f32.to_bits()));
        code.push(v_cmp_eq_u32(inline_u(1), 0)); // vcc = (tid == 1)
        code.extend_from_slice(&v_cndmask_b32(10, vgpr(14), vgpr(15), VCC));
        code.push(v_cmp_eq_u32(inline_u(2), 0)); // vcc = (tid == 2)
        code.extend_from_slice(&v_cndmask_b32(11, vgpr(14), vgpr(15), VCC));
        code.extend_from_slice(&v_literal(12, 0));
        code.extend_from_slice(&v_literal(13, 1.0f32.to_bits()));
        code.extend_from_slice(&v_literal(9, 0 | 1 << 10 | 2 << 20));
        code.push(0xF800_08CF); // exp pos0, v10..v13 done
        code.push(10 | 11 << 8 | 12 << 16 | 13 << 24);
        code.push(0xF800_0941); // exp prim, v9, off, off, off done
        code.push(9);
    }

    /// The console's dummy export for a group with nothing to draw: a null
    /// primitive (bit 31) and no position.
    fn emit_ms_culled_export(code: &mut Vec<u32>) {
        code.extend_from_slice(&v_literal(9, 0x8000_0000));
        code.push(0xF800_08C0); // exp pos0, off, off, off, off done
        code.push(0);
        code.push(0xF800_0941); // exp prim, v9, off, off, off done
        code.push(9);
    }

    fn ms_alloc_counts_program(culled: bool) -> NativeProgram {
        // (1, 1) is what the console's dummy path requests; (3, 3) matches
        // the triangle below.
        let packed = if culled { 0x1001 } else { 0x3003 };
        let mut code = ms_alloc_counts_head(packed);
        if culled {
            emit_ms_culled_export(&mut code);
        } else {
            emit_ms_screen_triangle(&mut code);
        }
        code.push(S_ENDPGM);
        make_ms_program(&code, 128, 0, 128, 128)
    }

    /// The translated forms must validate: one `setMeshOutputCounts` call
    /// (the validator rejects a call per path), reached by every thread and
    /// dominating every output write.
    #[test]
    fn mesh_alloc_counts_translate_and_validate() {
        for culled in [false, true] {
            let prog = ms_alloc_counts_program(culled);
            let shader = emit_ms(&prog, Wave64Mode::Emulate)
                .unwrap_or_else(|e| panic!("culled={culled}: {e}"));
            if let Ok(mut v) = super::super::validator::Validator::load(
                super::super::validator::Validator::default_path().as_deref(),
            ) {
                v.validate(&mut shader.dxil.clone())
                    .unwrap_or_else(|e| panic!("culled={culled}: validate: {e}"));
            }
        }
    }

    /// A group whose request allocates only the console's dummy slot (one
    /// vertex, one primitive) rasterizes nothing, while the same program
    /// with counts that declare the triangle draws it. This is the
    /// allocation-count overdraw: with the declared maxima instead of `m0`,
    /// the culled group would still launch 128 primitives.
    #[test]
    fn gpu_mesh_alloc_counts_culled_group_draws_nothing() {
        let draw = |groups| Draw {
            roots: Vec::new(),
            textures: &[],
            samplers: &[],
            vertices: &[],
            stride: 0,
            size: (8, 8),
            mesh_groups: groups,
        };
        let culled = ms_alloc_counts_program(true);
        let Some((host, root, pso, _)) = mesh_pso_with_ps("alloc_counts_culled", &culled, red_ps())
        else {
            return;
        };
        let pixels = host
            .draw(&root, &pso, &draw(1))
            .unwrap_or_else(|e| panic!("culled draw: {e}"));
        assert!(
            pixels.iter().all(|p| p[0] < 0.5),
            "a culled allocation-count group rasterized: {pixels:?}"
        );

        let real = ms_alloc_counts_program(false);
        let Some((host, root, pso, _)) = mesh_pso_with_ps("alloc_counts_real", &real, red_ps())
        else {
            return;
        };
        let pixels = host
            .draw(&root, &pso, &draw(1))
            .unwrap_or_else(|e| panic!("real draw: {e}"));
        assert!(
            pixels.iter().any(|p| p[0] > 0.5),
            "a 3-vertex allocation-count group rasterized nothing: {pixels:?}"
        );
    }

    /// The culled form must be cheap: unlike the declared maxima, its one
    /// allocated primitive is degenerate and never rasterizes.
    #[test]
    fn gpu_mesh_alloc_counts_culled_dispatch_cost() {
        let dispatch = Dispatch {
            roots: Vec::new(),
            views: &[],
            samplers: &[],
            groups: [1, 1, 1],
        };
        for (name, culled) in [("culled", true), ("real", false)] {
            let prog = ms_alloc_counts_program(culled);
            let Some((host, root, pso, _)) = mesh_pso(name, &prog) else {
                return;
            };
            host.dispatch_mesh_repeat(&root, &pso, &dispatch, 4, 2)
                .unwrap_or_else(|e| panic!("{name}: warm-up: {e}"));
            let count = 200u32;
            let start = std::time::Instant::now();
            host.dispatch_mesh_repeat(&root, &pso, &dispatch, 4, count)
                .unwrap_or_else(|e| panic!("{name}: dispatch: {e}"));
            let ms = start.elapsed().as_secs_f64() * 1e3 / f64::from(count);
            eprintln!("alloc counts {name}: {ms:.3} ms per 4-group dispatch");
            assert!(ms < 20.0, "alloc counts {name}: {ms:.3} ms per dispatch");
        }
    }

    #[test]
    fn gpu_64_threads_thread_id_only() {
        for (name, code, expected, addr_vreg) in [
            (
                "addr_tid_data_tid",
                vec![v_mov_b32(1, vgpr(0))],
                (0..64u32).collect::<Vec<_>>(),
                1u32,
            ),
            (
                "addr_tid_data_seven",
                vec![v_mov_b32(1, inline_u(7))],
                vec![7u32; 64],
                0u32,
            ),
        ] {
            let mut code = code;
            emit_uav_store(&mut code, 1, addr_vreg);
            code.push(S_ENDPGM);
            let prog = make_cs_program(&code, [64, 1, 1], 0, false);
            run_on_hosts(name, &prog, [1, 1, 1], 64, &expected, false);
        }
    }

    #[test]
    fn gpu_64_threads_thread_id_and_ballot_count() {
        // Regression: a 64-thread program whose only cross-lane read is a
        // ballot count must give every thread its own id, not zeros.
        let mut code = vec![
            v_mov_b32(1, vgpr(0)),           // v1 = SV_GroupThreadID
            v_cmp_lt_u32(inline_u(40), 0),    // vcc = tid > 40
            s_bcnt1_i32_b64(4, VCC),          // s4 = 23
            sop2(4, 0x1E, 4, inline_u(16)),   // s4 <<= 16
            v_or_b32(1, 4, 1),
        ];
        emit_uav_store(&mut code, 1, 0);
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [64, 1, 1], 0, false);
        let expected: Vec<u32> = (0..64u32).map(|i| i | (23 << 16)).collect();
        run_on_hosts("thread_id_and_ballot", &prog, [1, 1, 1], 64, &expected, true);
    }

    #[test]
    fn gpu_64_threads_mbcnt_ballot_and_high_ff1() {
        // v1 = mbcnt_64(-1) + bcnt1(tid > 34) * 1000 + ff1(tid > 34) * 100000
        // For tid in 0..64: active count = 29, first active bit = 35!
        let mut code = Vec::new();
        code.push(v_cmp_lt_u32(inline_u(34), 0)); // vcc = (34 < v0), bits 35..63 = 1
        code.push(s_bcnt1_i32_b64(4, VCC)); // s4 = 29
        code.push(s_ff1_i32_b64(5, VCC)); // s5 = 35
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        // Pack: lane | (bcnt << 8) | (ff1 << 16)
        code.push(v_lshlrev_b32(2, inline_u(8), 4)); // wrong if vop2 takes s4 in vsrc1; put s4 in src0!
        code.pop();
        // s_mov + v_mov for shifts using SGPR in src0:
        code.push(s_mov_b32(6, LIT));
        code.push(1000);
        code.push(s_mov_b32(7, LIT));
        code.push(100_000);
        // Wait: keep integer multiply simple using shifts (s_lshl_b32) + v_or_b32!
        code.clear();
        code.push(v_cmp_lt_u32(inline_u(34), 0)); // bits 35..63 set -> bcnt=29, ff1=35
        code.push(s_bcnt1_i32_b64(4, VCC));
        code.push(s_ff1_i32_b64(5, VCC));
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        // s6 = (s4 << 8) | (s5 << 16) = 29*256 + 35*65536
        code.push(sop2(4, 0x1E, 4, inline_u(8))); // s_lshl_b32 s4, s4, 8
        code.push(sop2(5, 0x1E, 5, inline_u(16))); // s_lshl_b32 s5, s5, 16
        code.push(sop2(4, 0x10, 4, 5)); // s_or_b32 s4, s4, s5
        code.push(v_or_b32(1, 4, 1));
        emit_uav_store(&mut code, 1, 0);
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [64, 1, 1], 0, false);
        let expected: Vec<u32> = (0..64u32).map(|i| i | (29 << 8) | (35 << 16)).collect();
        run_on_hosts("mbcnt_ballot_ff1", &prog, [1, 1, 1], 64, &expected, true);
    }

    #[test]
    fn gpu_128_threads_whole_wave_active_and_prefix() {
        // Virtual wave 0 (0..63), virtual wave 1 (64..127).
        // Value = (tid & 63) + 1; mask out odd console lanes via v_cmpx / s_and_saveexec?
        // Even simpler: active lanes = (tid & 1) == 0 via v_cmp_eq_u32 + s_and_saveexec_b64!
        let mut code = vec![
            v_and_b32(2, inline_u(63), 0),   // v2 = tid & 63
            v_add_nc_u32(2, inline_u(1), 2), // v2 = (tid & 63) + 1
            v_and_b32(3, inline_u(1), 0),    // v3 = tid & 1
            v_cmp_eq_u32(inline_u(0), 3),    // vcc = even lanes
            s_and_saveexec_b64(4, VCC),      // only even lanes active! (s[4:5] = all)
            s_or_saveexec_b64(6, NEG_ONE),   // s[6:7] = saved even-lane mask
        ];
        code.extend_from_slice(&v_cndmask_b32(10, inline_u(0), vgpr(2), 6));
        for shift in [0x111u32, 0x112, 0x114, 0x118] {
            code.extend_from_slice(&dpp_row_shr(0x25, 10, 10, 10, shift, 0xF));
        }
        // Store intra-row inclusive prefix in v11 before cross-row additions:
        code.push(v_mov_b32(11, vgpr(10)));
        code.extend_from_slice(&v_readlane_b32(12, 10, inline_u(15)));
        code.extend_from_slice(&v_readlane_b32(13, 10, inline_u(31)));
        code.extend_from_slice(&v_readlane_b32(14, 10, inline_u(47)));
        code.extend_from_slice(&v_readlane_b32(15, 10, inline_u(63)));
        // Broadcast cross-row sums for active sum (v10) and prefix offsets (v4 = s12+s13, v5 = v4+s14):
        code.push(v_mov_b32(4, 12));
        code.push(v_add_nc_u32(4, 13, 4));
        code.push(v_mov_b32(5, 14));
        code.push(v_add_nc_u32(5, vgpr(4), 5)); // v5 = s12+s13+s14
        code.push(v_mov_b32(10, 15));
        code.push(v_add_nc_u32(10, vgpr(5), 10)); // v10 = full 64-lane Active sum!
        // Apply prefix offsets to rows 1, 2, 3:
        code.push(s_mov_b32(EXEC_LO, LIT));
        code.push(0xFFFF_0000);
        code.push(s_mov_b32(EXEC_HI, inline_u(0)));
        code.push(v_add_nc_u32(11, 12, 11));
        code.push(s_mov_b32(EXEC_LO, inline_u(0)));
        code.push(s_mov_b32(EXEC_HI, LIT));
        code.push(0x0000_FFFF);
        code.push(v_add_nc_u32(11, vgpr(4), 11));
        code.push(s_mov_b32(EXEC_HI, LIT));
        code.push(0xFFFF_0000);
        code.push(v_add_nc_u32(11, vgpr(5), 11));
        code.push(s_mov_b64(EXEC_LO, 6)); // restore EXEC -> end of whole-wave section!
        code.push(v_sub_nc_u32(12, vgpr(2), 11)); // Wait: v_sub_nc_u32 vdst, src0, vsrc1 = src0 - vsrc1 (Wait: gfx10 vop2 sub is vdst = src0 - vsrc1 or vsrc1 - src0? Make sure: use v11 + v12 in exclusive!)
        code.pop();
        // Safer: v_sub_nc_u32_e32 v12, v2, v11 or let's use vop3 / check lower.rs! Actually Active (v10) and Inclusive Prefix (v11) already test Shape::Active and Shape::Prefix!
        // Pack: active | (prefix_incl << 12) for active even lanes; 0xFFFF for odd lanes!
        code.push(v_lshlrev_b32(11, inline_u(12), 11));
        code.push(v_or_b32(1, vgpr(10), 11));
        code.push(s_mov_b64(EXEC_LO, 4)); // restore all lanes
        // Write 1 for inactive odd lanes via v_cndmask: actually pre-init v1 = 0 before branch, keep simple: only even lanes store, odd stay 0 in UAV!
        // Wait: just write all active lanes -> odd lanes remain 0 in zeroed RawUav!
        code.pop(); // don't restore yet so odd lanes don't overwrite with garbage
        emit_uav_store(&mut code, 1, 0);
        code.push(s_mov_b64(EXEC_LO, 4));
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [128, 1, 1], 0, false);
        let even_sum_64: u32 = (0..64u32).filter(|l| l % 2 == 0).map(|l| l + 1).sum();
        let mut expected = Vec::with_capacity(128);
        for g in 0..128u32 {
            if g % 2 != 0 {
                expected.push(0);
            } else {
                let l = g & 63;
                let pref: u32 = (0..=l).filter(|k| k % 2 == 0).map(|k| k + 1).sum();
                expected.push(even_sum_64 | (pref << 12));
            }
        }
        run_on_hosts(
            "whole_wave_active_and_prefix",
            &prog,
            [1, 1, 1],
            128,
            &expected,
            true,
        );
    }

    #[test]
    fn gpu_96_threads_partial_virtual_wave() {
        // 96 threads: vwave 0 = 64 threads, vwave 1 = 32 threads (Half 0 only, NO Half 1!).
        // Tests both 64-lane ballot + Shape::Active across partial wave.
        let mut code = vec![
            v_and_b32(2, inline_u(63), 0),
            v_add_nc_u32(2, inline_u(1), 2), // 1..=64 in w0, 1..=32 in w1
            v_cmp_lt_u32(inline_u(15), 2),   // count lanes > 15: 49 in w0, 17 in w1
            s_bcnt1_i32_b64(4, VCC),
            s_or_saveexec_b64(6, NEG_ONE),
        ];
        code.extend_from_slice(&v_cndmask_b32(10, inline_u(0), vgpr(2), 6));
        for shift in [0x111u32, 0x112, 0x114, 0x118] {
            code.extend_from_slice(&dpp_row_shr(0x25, 10, 10, 10, shift, 0xF));
        }
        code.extend_from_slice(&v_readlane_b32(12, 10, inline_u(15)));
        code.extend_from_slice(&v_readlane_b32(13, 10, inline_u(31)));
        code.extend_from_slice(&v_readlane_b32(14, 10, inline_u(47)));
        code.extend_from_slice(&v_readlane_b32(15, 10, inline_u(63)));
        code.push(v_mov_b32(10, 12));
        code.push(v_add_nc_u32(10, 13, 10));
        code.push(v_add_nc_u32(10, 14, 10));
        code.push(v_add_nc_u32(10, 15, 10));
        code.push(s_mov_b64(EXEC_LO, 6));

        code.push(sop2(4, 0x1E, 4, inline_u(16))); // s_lshl_b32 s4, s4, 16 (bcnt << 16)
        code.push(v_or_b32(1, 4, 10));
        emit_uav_store(&mut code, 1, 0);
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [96, 1, 1], 0, false);
        let mut expected = Vec::with_capacity(96);
        let sum0: u32 = (1..=64).sum();
        let sum1: u32 = (1..=32).sum();
        for g in 0..96u32 {
            if g < 64 {
                expected.push(sum0 | (49 << 16));
            } else {
                expected.push(sum1 | (17 << 16));
            }
        }
        run_on_hosts(
            "partial_virtual_wave_96",
            &prog,
            [1, 1, 1],
            96,
            &expected,
            true,
        );
    }

    #[test]
    fn gpu_128_threads_readlane_writelane_spills_and_firstlane() {
        // Tests:
        // 1. Dynamic cross-half v_readlane_b32 reading partner = (tid ^ 33) & 63
        // 2. Non-uniform v_readfirstlane_b32 picking lane 0 (0 in w0, 64 in w1)
        // 3. Constant-lane SGPR spill via v_writelane_b32(_, _, 35) -> v_readlane_b32(_, 35) even inside divergent if!
        let mut code = Vec::new();
        // Spill s12 = 77 into v3 lane 35; keep in v1 when tid < 48 (divergent branch!)
        code.push(s_mov_b32(12, LIT));
        code.push(77);
        code.extend_from_slice(&v_writelane_b32(3, 12, inline_u(35)));
        code.push(v_mov_b32(4, inline_u(0)));
        code.push(v_cmp_gt_u32(inline_u(48), 0)); // 48 > v0 i.e. v0 < 48
        code.push(s_and_saveexec_b64(6, VCC));
        let br_idx = code.len();
        code.push(0);
        code.extend_from_slice(&v_readlane_b32(13, 3, inline_u(35)));
        code.push(v_mov_b32(4, 13));
        let after = code.len();
        code[br_idx] = s_cbranch_execz((after - (br_idx + 1)) as i16);
        code.push(s_or_b64(EXEC_LO, EXEC_LO, 6));

        // Non-uniform v_readfirstlane_b32 of tid (`v0`): 0 for w0, 64 for w1!
        code.push(v_readfirstlane_b32(14, 0));

        // Dynamic v_readlane_b32 reading (tid ^ 33) & 63 across halves:
        code.push(v_mov_b32(5, vgpr(0)));
        code.push(vop2(0x1D, 5, inline_u(33), 5)); // v_xor_b32 v5, 33, v5
        code.push(v_and_b32(5, inline_u(63), 5));
        code.extend_from_slice(&v_readlane_b32(15, 0, vgpr(5)));

        // Result = spill (4) + readfirstlane (s14 << 8) + dynamic_readlane (s15 << 16)
        code.push(sop2(14, 0x1E, 14, inline_u(8)));
        code.push(v_add_nc_u32(1, 14, 4));
        code.push(sop2(15, 0x1E, 15, inline_u(16)));
        code.push(v_add_nc_u32(1, 15, 1));
        emit_uav_store(&mut code, 1, 0);
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [128, 1, 1], 0, false);
        let expected: Vec<u32> = (0..128u32)
            .map(|g| {
                let wbase = g & !63;
                let spill = if g < 48 { 77 } else { 0 };
                let first = wbase;
                let partner = wbase | ((g ^ 33) & 63);
                spill + (first << 8) + (partner << 16)
            })
            .collect();
        run_on_hosts(
            "readlane_spills_firstlane",
            &prog,
            [1, 1, 1],
            128,
            &expected,
            true,
        );
    }

    #[test]
    fn gpu_128_threads_ds_swizzle_and_dpp_row_mask() {
        // 1. ds_swizzle_b32 XOR 2 within 32-lane halves (offset = 0x1F | (2 << 10) = 0x81F)
        // 2. v_add_nc_u32_dpp row_shr:1 bound_ctrl:1 with row_mask = 0xA (rows 1 & 3 enabled, rows 0 & 2 masked!)
        let mut code = Vec::new();
        code.extend_from_slice(&ds_op(0x35, 0x1F, 0x08, 0, 0, 2)); // v2 = swizzle(v0, xor 2)
        code.push(v_mov_b32(3, inline_u(1))); // fallback 1 on masked rows 0 and 2!
        // src1=v0, src0=v0 (dpp row_shr:1 -> prev_tid), row_mask=0xA (bits 1 and 3!), bank_mask=0xF, bound_ctrl=1
        code.extend_from_slice(&dpp_row_shr(0x25, 3, 0, 0, 0x111, 0xA));
        code.push(v_lshlrev_b32(3, inline_u(16), 3));
        code.push(v_or_b32(1, vgpr(2), 3));
        emit_uav_store(&mut code, 1, 0);
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [128, 1, 1], 0, false);
        let expected: Vec<u32> = (0..128u32)
            .map(|g| {
                let swz = g ^ 2;
                let row = (g & 63) >> 4;
                let dpp = if row == 1 || row == 3 {
                    if (g & 15) == 0 { g } else { g + (g - 1) }
                } else {
                    1
                };
                swz | (dpp << 16)
            })
            .collect();
        run_on_hosts(
            "ds_swizzle_dpp_rowmask",
            &prog,
            [1, 1, 1],
            128,
            &expected,
            true,
        );
    }

    #[test]
    fn gpu_64_threads_lockstep_lds_without_s_barrier() {
        // 64 threads sharing LDS without `s_barrier` (assumes single Wave64 wave on console).
        let mut code = Vec::new();
        code.push(v_lshlrev_b32(2, inline_u(2), 0)); // write addr = tid * 4
        code.push(v_add_nc_u32(3, inline_u(7), 0)); // val = tid + 7
        code.extend_from_slice(&ds_op(0x0D, 0, 0, 2, 3, 0)); // ds_write_b32 addr=v2, data=v3
        code.push(vop2(0x1D, 4, inline_u(32), 0)); // partner = tid ^ 32 (crosses Wave32 half!)
        code.push(v_lshlrev_b32(4, inline_u(2), 4)); // read addr = partner * 4
        code.extend_from_slice(&ds_op(0x36, 0, 0, 4, 0, 1)); // ds_read_b32 v1, addr=v4
        emit_uav_store(&mut code, 1, 0);
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [64, 1, 1], 512, false);
        let expected: Vec<u32> = (0..64u32).map(|i| (i ^ 32) + 7).collect();
        run_on_hosts(
            "lockstep_lds_no_barrier",
            &prog,
            [1, 1, 1],
            64,
            &expected,
            false,
        );
    }

    #[test]
    fn gpu_128_threads_uniform_loop_cross_half_ballot() {
        // Uniform scalar loop for s10 in 0..4 performing 64-lane ballot every iteration.
        let mut code = Vec::new();
        code.push(s_mov_b32(10, inline_u(0))); // loop counter
        code.push(v_mov_b32(1, inline_u(0))); // accumulator
        let loop_top = code.len();
        code.push(v_and_b32(2, inline_u(63), 0));
        code.push(v_add_nc_u32(2, 10, 2)); // (tid & 63) + iter
        code.push(v_cmp_lt_u32(inline_u(40), 2)); // count increases by 1 each iteration!
        code.push(s_bcnt1_i32_b64(4, VCC));
        code.push(v_add_nc_u32(1, 4, 1));
        code.push(s_add_i32(10, 10, inline_u(1)));
        code.push(s_cmp_lt_u32(10, inline_u(4)));
        let br = code.len();
        code.push(s_cbranch_scc1(
            (loop_top as isize - (br + 1) as isize) as i16,
        ));
        emit_uav_store(&mut code, 1, 0);
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [128, 1, 1], 0, false);
        // iter 0: >40 in 0..=63 => 23; iter 1: 24; iter 2: 25; iter 3: 26 => sum = 98
        let expected = vec![23 + 24 + 25 + 26; 128];
        run_on_hosts(
            "uniform_loop_ballots",
            &prog,
            [1, 1, 1],
            128,
            &expected,
            true,
        );
    }

    #[test]
    fn gpu_32_threads_wave32_fast_path_with_high_lane_spill() {
        // 32-thread Wave64 compute shader: Wave32 uses single wave + zero LDS scratch,
        // while still resolving constant-lane SGPR spill at lane 45 (>= 32).
        let mut code = Vec::new();
        code.push(s_mov_b32(12, LIT));
        code.push(123);
        code.extend_from_slice(&v_writelane_b32(3, 12, inline_u(45)));
        code.extend_from_slice(&v_readlane_b32(13, 3, inline_u(45)));
        code.push(v_add_nc_u32(1, 13, 0));
        emit_uav_store(&mut code, 1, 0);
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [32, 1, 1], 0, false);
        let expected: Vec<u32> = (0..32u32).map(|i| 123 + i).collect();
        run_on_hosts(
            "wave32_32_threads_spill",
            &prog,
            [1, 1, 1],
            32,
            &expected,
            true,
        );
    }

    #[test]
    fn gpu_128_threads_cross_block_ballot_and_constant_mask_guard() {
        // Block 0 defines `vcc` (`tid & 63 >= 40`), branches uniformly via s_branch (block split)
        // to block 1 where `s_bcnt1` consumes cross-block `vcc`, plus `Atom::Constant` branch mask (`-16` -> lanes 4..63).
        let mut code = vec![
            v_and_b32(2, inline_u(63), 0),
            v_cmp_lt_u32(inline_u(39), 2), // vcc = (tid & 63) >= 40 (24 bits)
            sopp(0x02, 0),                 // s_branch +0 -> block boundary!
            // Block 1:
            s_bcnt1_i32_b64(4, VCC),    // s4 = 24
            s_and_saveexec_b64(8, 208), // 208 = inline -16 (bits 4..63 active in Wave64)
        ];
        let br_idx = code.len();
        code.push(0); // s_cbranch_execz -> after
        code.push(v_add_nc_u32(1, 4, 0));
        emit_uav_store(&mut code, 1, 0);
        let after = code.len();
        code[br_idx] = s_cbranch_execz((after - (br_idx + 1)) as i16);
        code.push(s_or_b64(EXEC_LO, EXEC_LO, 8));
        code.push(S_ENDPGM);

        let prog = make_cs_program(&code, [128, 1, 1], 0, false);
        let expected: Vec<u32> = (0..128u32)
            .map(|g| if (g & 63) >= 4 { 24 + g } else { 0 })
            .collect();
        run_on_hosts(
            "cross_block_and_const_mask",
            &prog,
            [1, 1, 1],
            128,
            &expected,
            true,
        );
    }

    /// Every lane's console lane (via `v_mbcnt`) and a ballot count over the
    /// `lane > 40` mask, packed as `lane | bcnt << 16`.
    fn ms_lane_ballot_program() -> Vec<u32> {
        let mut code = Vec::new();
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        code.push(v_mov_b32(7, vgpr(0))); // v7 = SV_GroupIndex (store address)
        code.push(v_mov_b32(2, vgpr(1)));
        code.push(v_cmp_lt_u32(inline_u(40), 1));
        code.push(s_bcnt1_i32_b64(4, VCC));
        code.push(sop2(4, 0x1E, 4, inline_u(16)));
        code.push(v_or_b32(2, 4, 2));
        emit_ms_uav_store(&mut code, 2, 7);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);
        code
    }

    #[test]
    fn gpu_96_threads_3d_group_lane_mapping() {
        // A 3-D group (4,4,6): lane mapping must come from the flattened
        // thread id, and the last 32 threads form a partial console wave.
        let mut code = vec![
            v_mov_b32(3, vgpr(0)),           // v3 = x
            v_lshlrev_b32(5, inline_u(3), 1), // v5 = y * 8
            v_lshlrev_b32(6, inline_u(1), 1), // v6 = y * 2
            v_add_nc_u32(3, vgpr(5), 3),
            v_add_nc_u32(3, vgpr(6), 3),          // v3 = x + 10y
            v_lshlrev_b32(5, inline_u(6), 2),     // z * 64
            v_lshlrev_b32(6, inline_u(5), 2),     // z * 32
            v_add_nc_u32(3, vgpr(5), 3),
            v_add_nc_u32(3, vgpr(6), 3),
            v_lshlrev_b32(5, inline_u(2), 2), // z * 4
            v_add_nc_u32(3, vgpr(5), 3),      // v3 = x + 10y + 100z
            // v4 = flattened id: x + 4y + 16z
            v_mov_b32(4, vgpr(0)),
            v_lshlrev_b32(5, inline_u(2), 1),
            v_add_nc_u32(4, vgpr(5), 4),
            v_lshlrev_b32(5, inline_u(4), 2),
            v_add_nc_u32(4, vgpr(5), 4),
            v_cmp_lt_u32(inline_u(40), 4),
            s_bcnt1_i32_b64(6, VCC),
        ];
        code.push(sop2(6, 0x1E, 6, inline_u(16)));
        code.push(v_or_b32(3, 6, 3));
        emit_uav_store(&mut code, 3, 4);
        code.push(S_ENDPGM);

        let mut prog = make_cs_program(&code, [4, 4, 6], 0, false);
        // All three thread-id VGPRs (the helper enables only v0).
        if let StageIo::Cs(ref mut cs) = prog.io {
            cs.rsrc2 |= 2 << 11;
        }
        let expected: Vec<u32> = (0..96u32)
            .map(|flat| {
                let (x, y, z) = (flat & 3, flat >> 2 & 3, flat >> 4);
                let bcnt = if flat < 64 { 23 } else { 32 };
                (x + 10 * y + 100 * z) | (bcnt << 16)
            })
            .collect();
        run_on_hosts(
            "three_d_group_96",
            &prog,
            [1, 1, 1],
            96,
            &expected,
            true,
        );
    }

    #[test]
    fn gpu_mesh_64_threads_one_console_wave() {
        let prog = make_ms_program(&ms_lane_ballot_program(), 64, 0, 64, 1);
        let expected: Vec<u32> = (0..64u32).map(|i| i | (23 << 16)).collect();
        run_mesh("ms_64_lane_ballot", &prog, 1, 64, &expected);
    }

    #[test]
    fn gpu_mesh_96_threads_partial_last_console_wave() {
        let prog = make_ms_program(&ms_lane_ballot_program(), 96, 0, 96, 1);
        let mut expected = Vec::new();
        for g in 0..96u32 {
            let lane = g & 63;
            let bcnt = if g < 64 { 23 } else { 0 };
            expected.push(lane | (bcnt << 16));
        }
        run_mesh("ms_96_lane_ballot", &prog, 1, 96, &expected);
    }

    #[test]
    fn gpu_mesh_128_threads_cross_wave_readlane() {
        // Each lane reads (lane * 2 + 1) from lane (lane + 1) & 63.
        let mut code = Vec::new();
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        code.push(v_mov_b32(6, vgpr(0))); // v6 = SV_GroupIndex (store address)
        code.push(v_mov_b32(5, vgpr(1))); // v5 = lane
        code.push(v_lshlrev_b32(2, inline_u(1), 1)); // v2 = lane * 2
        code.push(v_add_nc_u32(2, inline_u(1), 2)); // v2 = lane * 2 + 1
        code.push(v_add_nc_u32(3, inline_u(1), 1)); // v3 = lane + 1
        code.push(v_and_b32(3, inline_u(63), 3)); // v3 = (lane + 1) & 63
        code.extend_from_slice(&v_readlane_b32(4, 2, vgpr(3)));
        code.push(sop2(4, 0x1E, 4, inline_u(16)));
        code.push(v_or_b32(1, 4, 1));
        emit_ms_uav_store(&mut code, 1, 6);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);

        let prog = make_ms_program(&code, 128, 0, 128, 1);
        let expected: Vec<u32> = (0..128u32)
            .map(|g| {
                let lane = g & 63;
                lane | ((((lane + 1) & 63) * 2 + 1) << 16)
            })
            .collect();
        run_mesh("ms_128_readlane", &prog, 1, 128, &expected);
    }

    #[test]
    fn gpu_mesh_64_threads_ds_swizzle_across_halves() {
        // v2 = swizzle XOR 2 of v1, where v1 = lane.
        let mut code = Vec::new();
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        code.push(v_mov_b32(5, vgpr(1))); // v5 = lane (store address)
        code.extend_from_slice(&ds_op(0x35, 0x1F, 0x08, 1, 0, 2)); // v2 = swizzle(v1, xor 2)
        code.push(v_lshlrev_b32(2, inline_u(16), 2));
        code.push(v_or_b32(1, vgpr(2), 1));
        emit_ms_uav_store(&mut code, 1, 5);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);

        let prog = make_ms_program(&code, 64, 0, 64, 1);
        let expected: Vec<u32> = (0..64u32).map(|i| i | ((i ^ 2) << 16)).collect();
        run_mesh("ms_64_swizzle", &prog, 1, 64, &expected);
    }

    #[test]
    fn gpu_mesh_64_threads_lds_without_barrier() {
        // 64 console threads share LDS in lockstep: each reads (lane ^ 32) + 7.
        let mut code = Vec::new();
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        code.push(v_mov_b32(6, vgpr(1))); // v6 = lane (store address)
        code.push(v_lshlrev_b32(2, inline_u(2), 1)); // addr = lane * 4
        code.push(v_add_nc_u32(3, inline_u(7), 1)); // val = lane + 7
        code.extend_from_slice(&ds_op(0x0D, 0, 0, 2, 3, 0)); // ds_write_b32
        code.push(vop2(0x1D, 4, inline_u(32), 1)); // v4 = lane ^ 32
        code.push(v_lshlrev_b32(4, inline_u(2), 4));
        code.extend_from_slice(&ds_op(0x36, 0, 0, 4, 0, 5)); // ds_read_b32 v5
        code.push(v_lshlrev_b32(5, inline_u(16), 5));
        code.push(v_or_b32(1, vgpr(5), 1));
        emit_ms_uav_store(&mut code, 1, 6);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);

        let prog = make_ms_program(&code, 64, 512, 64, 1);
        let expected: Vec<u32> = (0..64u32).map(|i| i | (((i ^ 32) + 7) << 16)).collect();
        run_mesh("ms_64_lds_lockstep", &prog, 1, 64, &expected);
    }

    #[test]
    fn gpu_mesh_64_threads_title_lds_and_swizzle_scratch() {
        // The program's own LDS (64 dwords) and the emulation's exchange
        // scratch must not overlap: write lane + 7 to LDS[lane], read it back,
        // and swizzle (OR 8 within each 32-lane half) through the scratch.
        let mut code = Vec::new();
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        code.push(v_mov_b32(6, vgpr(0))); // store address = flat id
        code.push(v_lshlrev_b32(2, inline_u(2), 1)); // LDS byte address = lane * 4
        code.push(v_add_nc_u32(3, inline_u(7), 1)); // value = lane + 7
        code.extend_from_slice(&ds_op(0x0D, 0, 0, 2, 3, 0)); // ds_write_b32
        code.extend_from_slice(&ds_op(0x35, 0, 1, 1, 0, 3)); // v3 = swizzle OR 8
        code.extend_from_slice(&ds_op(0x36, 0, 0, 2, 0, 4)); // v4 = ds_read_b32
        code.push(v_lshlrev_b32(3, inline_u(16), 3)); // swizzled << 16
        code.push(v_or_b32(1, vgpr(3), 4)); // data = value | swizzled << 16
        emit_ms_uav_store(&mut code, 1, 6);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);

        let prog = make_ms_program(&code, 64, 256, 64, 1);
        let expected: Vec<u32> = (0..64u32)
            .map(|lane| (lane + 7) | (((lane & !31) | 8) << 16))
            .collect();
        run_mesh("ms_64_title_lds_and_scratch", &prog, 1, 64, &expected);
    }

    #[test]
    fn gpu_mesh_64_threads_readfirstlane_uniform_in_divergent_flow() {
        // A uniform value's first-lane read is the value itself: it must not
        // need the group-wide exchange, which a lane guard would deadlock.
        let mut code = Vec::new();
        code.push(s_mov_b32(10, LIT));
        code.push(0x1234);
        code.push(v_mov_b32(2, 10)); // v2 = s10 (uniform)
        code.push(v_cmp_gt_u32(inline_u(32), 0)); // vcc = 32 > tid
        code.push(s_and_saveexec_b64(6, VCC));
        let br = code.len();
        code.push(0);
        code.push(v_readfirstlane_b32(12, 2));
        code.push(v_mov_b32(5, 12));
        let after = code.len();
        code[br] = s_cbranch_execz((after - (br + 1)) as i16);
        code.push(s_or_b64(EXEC_LO, EXEC_LO, 6));
        code.push(v_mov_b32(7, vgpr(0))); // store address = flat id
        emit_ms_uav_store(&mut code, 5, 7);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);

        let prog = make_ms_program(&code, 64, 0, 64, 1);
        let expected: Vec<u32> = (0..64u32)
            .map(|g| if g < 32 { 0x1234 } else { 0 })
            .collect();
        run_mesh("ms_64_uniform_firstlane_divergent", &prog, 1, 64, &expected);
    }

    #[test]
    fn gpu_mesh_64_threads_readfirstlane_and_ff1() {
        // A lane-varying value in uniform flow: lane 0's value (1) and the
        // first ballot lane above 40 (41) broadcast through the scratch.
        let mut code = Vec::new();
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        code.push(v_mov_b32(7, vgpr(0))); // store address = flat id
        code.push(v_lshlrev_b32(2, inline_u(1), 1)); // v2 = lane * 2
        code.push(v_add_nc_u32(2, inline_u(1), 2)); // v2 = lane * 2 + 1
        code.push(v_readfirstlane_b32(4, 2)); // lane 0: 1
        code.push(v_cmp_lt_u32(inline_u(40), 1)); // vcc = lane > 40
        code.push(s_ff1_i32_b64(5, VCC)); // 41
        code.push(sop2(5, 0x1E, 5, inline_u(8)));
        code.push(v_or_b32(1, 5, 1)); // lane | 41 << 8
        code.push(sop2(4, 0x1E, 4, inline_u(16)));
        code.push(v_or_b32(1, 4, 1)); // | 1 << 16
        emit_ms_uav_store(&mut code, 1, 7);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);

        let prog = make_ms_program(&code, 64, 0, 64, 1);
        let expected: Vec<u32> = (0..64u32).map(|g| g | (41 << 8) | (1 << 16)).collect();
        run_mesh("ms_64_firstlane_ff1", &prog, 1, 64, &expected);
    }

    #[test]
    fn gpu_mesh_without_console_lane_code_stays_unemulated() {
        // Only the thread index and a UAV store: no virtual wave machinery.
        let mut code = vec![
            v_mov_b32(1, vgpr(0)),
            v_mov_b32(2, vgpr(1)),
            v_add_nc_u32(1, inline_u(9), 1),
        ];
        emit_ms_uav_store(&mut code, 1, 2);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);
        let prog = make_ms_program(&code, 64, 0, 64, 1);
        let expected: Vec<u32> = (0..64u32).map(|i| i + 9).collect();
        run_mesh("ms_no_console_lanes", &prog, 1, 64, &expected);
    }

    fn psv_mesh_shared(shader: &HostShader) -> u32 {
        let c = Container::parse(&shader.dxil).unwrap();
        u32::from_le_bytes(
            c.part(FourCC::PSV0).unwrap().data[4..8]
                .try_into()
                .unwrap(),
        )
    }

    #[test]
    fn mesh_lane_only_skips_lds_and_barriers() {
        for threads in [64u32, 128] {
            let fast = emit_ms(
                &make_ms_program(&ms_firstlane_loop_program(1, 8, false), threads, 0, threads, 1),
                Wave64Mode::Native,
            )
            .unwrap();
            assert_eq!(psv_mesh_shared(&fast), 0, "{threads}-thread lane-only LDS");
            let emulated = emit_ms(
                &make_ms_program(&ms_firstlane_loop_program(1, 8, true), threads, 0, threads, 1),
                Wave64Mode::Native,
            )
            .unwrap();
            assert!(
                psv_mesh_shared(&emulated) > 0,
                "{threads}-thread forced exchange has no LDS"
            );
        }
    }

    #[test]
    fn gpu_mesh_fast_path_lane_and_constant_mask() {
        // Lane number + quad swizzle + a constant lane mask: console lane
        // arithmetic only, no exchange.
        for threads in [64u32, 128] {
            let mut code = Vec::new();
            code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
            code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
            code.push(v_mov_b32(2, vgpr(0))); // store address = flat id
            code.extend_from_slice(&ds_op(0x35, 0x1F, 0x08, 1, 0, 3)); // v3 = lane xor 2
            code.push(v_mov_b32(4, inline_u(0)));
            code.push(s_and_saveexec_b64(6, 208)); // lanes 4..63
            let br = code.len();
            code.push(0);
            code.push(v_mov_b32(4, inline_u(1)));
            let after = code.len();
            code[br] = s_cbranch_execz((after - (br + 1)) as i16);
            code.push(s_or_b64(EXEC_LO, EXEC_LO, 6));
            code.push(v_lshlrev_b32(3, inline_u(8), 3));
            code.push(v_or_b32(1, vgpr(3), 1));
            code.push(v_lshlrev_b32(4, inline_u(16), 4));
            code.push(v_or_b32(1, vgpr(4), 1));
            emit_ms_uav_store(&mut code, 1, 2);
            emit_ms_position(&mut code);
            code.push(S_ENDPGM);

            let prog = make_ms_program(&code, threads, 0, threads, 1);
            let Some((host, root, pso, shader)) = mesh_pso("fast_lane_mask", &prog) else {
                return;
            };
            assert_eq!(psv_mesh_shared(&shader), 0);
            let out = host
                .dispatch_mesh(
                    &root,
                    &pso,
                    &Dispatch {
                        roots: Vec::new(),
                        views: &[View::RawUav(threads)],
                        samplers: &[],
                        groups: [1, 1, 1],
                    },
                    1,
                )
                .unwrap_or_else(|e| panic!("fast_lane_mask: {e}"));
            let expected: Vec<u32> = (0..threads)
                .map(|g| {
                    let lane = g & 63;
                    let swz = lane ^ 2;
                    let guarded = if lane >= 4 { 1 } else { 0 };
                    lane | (swz << 8) | (guarded << 16)
                })
                .collect();
            assert_eq!(&out[0][..expected.len()], &expected[..], "fast_lane_mask");
        }
    }

    /// A loop of `uniform_firstlanes` reads of a uniform value, repeated
    /// `iters` times; `force_emulation` adds a non-quad swizzle so the
    /// program keeps the full cross-lane machinery.
    fn ms_firstlane_loop_program(iters: u32, uniform_firstlanes: usize, force_emulation: bool) -> Vec<u32> {
        let mut code = Vec::new();
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        code.push(v_mov_b32(3, vgpr(0))); // store address = flat id
        code.push(s_mov_b32(10, LIT));
        code.push(0x1234);
        code.push(v_mov_b32(2, 10)); // v2 = uniform 0x1234
        if force_emulation {
            // Non-quad swizzle (OR 8 within each 32-lane half): forces the
            // emulated exchange on top of the first-lane reads.
            code.extend_from_slice(&ds_op(0x35, 0, 1, 1, 0, 14));
        }
        code.push(s_mov_b32(20, inline_u(0)));
        let loop_start = code.len();
        for _ in 0..uniform_firstlanes {
            code.push(v_readfirstlane_b32(12, 2));
        }
        code.push(s_add_i32(20, 20, inline_u(1)));
        code.push(s_cmp_lt_u32(20, inline_u(iters)));
        let br = code.len();
        code.push(s_cbranch_scc1(
            (loop_start as isize - (br + 1) as isize) as i16,
        ));
        if force_emulation {
            code.push(v_or_b32(1, vgpr(14), 1));
        }
        code.push(sop2(12, 0x1E, 12, inline_u(16)));
        code.push(v_or_b32(1, 12, 1));
        emit_ms_uav_store(&mut code, 1, 3);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);
        code
    }

    /// Non-uniform `v_readfirstlane` (lane 0's value) repeated in a loop, to
    /// time the emulated exchange's barriers.
    fn ms_firstlane_nonuniform_loop_program(iters: u32) -> Vec<u32> {
        let mut code = Vec::new();
        code.extend_from_slice(&v_mbcnt_lo_u32_b32(1, NEG_ONE, inline_u(0)));
        code.extend_from_slice(&v_mbcnt_hi_u32_b32(1, NEG_ONE, vgpr(1)));
        code.push(v_mov_b32(3, vgpr(0))); // store address = flat id
        code.push(s_mov_b32(21, LIT));
        code.push(iters);
        code.push(s_mov_b32(20, inline_u(0)));
        let loop_start = code.len();
        for _ in 0..8 {
            code.push(v_readfirstlane_b32(12, 1)); // lane 0's value
        }
        code.push(s_add_i32(20, 20, inline_u(1)));
        code.push(s_cmp_lt_u32(20, 21));
        let br = code.len();
        code.push(s_cbranch_scc1(
            (loop_start as isize - (br + 1) as isize) as i16,
        ));
        code.push(sop2(12, 0x1E, 12, inline_u(16)));
        code.push(v_or_b32(1, 12, 1));
        emit_ms_uav_store(&mut code, 1, 3);
        emit_ms_position(&mut code);
        code.push(S_ENDPGM);
        code
    }

    #[test]
    fn gpu_mesh_firstlane_exchange_dispatch_cost() {
        for threads in [64u32, 128] {
            let prog = make_ms_program(&ms_firstlane_nonuniform_loop_program(1024), threads, 0, threads, 1);
            let name = format!("bench_firstlane_{threads}");
            let Some((host, root, pso, _)) = mesh_pso(&name, &prog) else {
                return;
            };
            let dispatch = Dispatch {
                roots: Vec::new(),
                views: &[View::RawUav(threads)],
                samplers: &[],
                groups: [1, 1, 1],
            };
            host.dispatch_mesh_repeat(&root, &pso, &dispatch, 1, 1)
                .unwrap_or_else(|e| panic!("{name}: warmup: {e}"));
            let count = 5u32;
            let start = std::time::Instant::now();
            host.dispatch_mesh_repeat(&root, &pso, &dispatch, 1, count)
                .unwrap_or_else(|e| panic!("{name}: dispatch: {e}"));
            let per = start.elapsed().as_secs_f64() * 1000.0 / f64::from(count);
            eprintln!(
                "{name}: {per:.3} ms per dispatch (1024 loop iterations, 8 non-uniform firstlanes)"
            );
            assert!(per < 1000.0, "{name}: {per:.3} ms per dispatch");
        }
    }

    #[test]
    fn gpu_mesh_lane_only_dispatch_cost() {
        for (threads, iters, force, label) in [
            (64u32, 1u32, false, "fast_1"),
            (128u32, 1u32, false, "fast_1"),
            (64u32, 64u32, false, "fast_64"),
            (128u32, 64u32, false, "fast_64"),
            (64u32, 64u32, true, "emulated_64"),
            (128u32, 64u32, true, "emulated_64"),
        ] {
            let code = ms_firstlane_loop_program(iters, 8, force);
            let prog = make_ms_program(&code, threads, 0, threads, 1);
            let name = format!("bench_{threads}_{label}");
            let Some((host, root, pso, shader)) = mesh_pso(&name, &prog) else {
                return;
            };
            let psv = Container::parse(&shader.dxil).unwrap();
            let shared = u32::from_le_bytes(
                psv.part(FourCC::PSV0).unwrap().data[4..8]
                    .try_into()
                    .unwrap(),
            );
            let dispatch = Dispatch {
                roots: Vec::new(),
                views: &[View::RawUav(threads)],
                samplers: &[],
                groups: [1, 1, 1],
            };
            // Warm up the pipeline, then time a run of dispatches in one list.
            host.dispatch_mesh_repeat(&root, &pso, &dispatch, 1, 2)
                .unwrap_or_else(|e| panic!("{name}: warmup: {e}"));
            let count = 20u32;
            let start = std::time::Instant::now();
            host.dispatch_mesh_repeat(&root, &pso, &dispatch, 1, count)
                .unwrap_or_else(|e| panic!("{name}: dispatch: {e}"));
            let per = start.elapsed().as_secs_f64() * 1000.0 / f64::from(count);
            eprintln!(
                "{name}: {per:.3} ms per dispatch ({iters} loop iterations, 8 firstlanes; LDS {shared} B)"
            );
        }
    }

    #[test]
    fn a_mesh_wave_rendezvous_stops_at_the_spin_limit() {
        use std::sync::atomic::Ordering;
        super::super::lower::WAVE_BARRIER_LIMIT.store(0, Ordering::Relaxed);
        let prog = make_ms_program(&ms_firstlane_nonuniform_loop_program(1), 128, 0, 128, 1);
        emit_ms(&prog, Wave64Mode::Native).unwrap();
        let limit = super::super::lower::WAVE_BARRIER_LIMIT.load(Ordering::Relaxed);
        assert_eq!(limit, super::super::spin_limit());
    }
}
