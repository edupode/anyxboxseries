use core::sync::atomic::{AtomicU32, AtomicUsize, Ordering};

use crate::hooks::single_rule;
use crate::iat;
use crate::common::record;

const M: &str = "xmem";

static UNMAP_ORIG: AtomicUsize = AtomicUsize::new(0);
static UNMAPEX_ORIG: AtomicUsize = AtomicUsize::new(0);
static UNMAP2_ORIG: AtomicUsize = AtomicUsize::new(0);
static PROTECT_ORIG: AtomicUsize = AtomicUsize::new(0);

unsafe extern "system" fn unmap_w(base: usize) -> i32 {
    let (lo, hi) = crate::epoch::bump_free(base, 0);
    let r = core::mem::transmute::<usize, unsafe extern "system" fn(usize) -> i32>(UNMAP_ORIG.load(Ordering::Acquire))(base);
    crate::epoch::bump_range(lo, hi);
    r
}

unsafe extern "system" fn unmapex_w(base: usize, flags: u32) -> i32 {
    let (lo, hi) = crate::epoch::bump_free(base, 0);
    let r = core::mem::transmute::<usize, unsafe extern "system" fn(usize, u32) -> i32>(UNMAPEX_ORIG.load(Ordering::Acquire))(base, flags);
    crate::epoch::bump_range(lo, hi);
    r
}

unsafe extern "system" fn unmap2_w(process: usize, base: usize, flags: u32) -> i32 {
    let (lo, hi) = crate::epoch::bump_free(base, 0);
    let r = core::mem::transmute::<usize, unsafe extern "system" fn(usize, usize, u32) -> i32>(UNMAP2_ORIG.load(Ordering::Acquire))(process, base, flags);
    crate::epoch::bump_range(lo, hi);
    r
}

unsafe extern "system" fn protect_w(addr: usize, size: usize, new: u32, old: *mut u32) -> i32 {
    let lo = addr & !0xfff;
    let hi = (addr.saturating_add(size.max(1)) + 0xfff) & !0xfff;
    crate::epoch::bump_range(lo, hi);
    let r = core::mem::transmute::<usize, unsafe extern "system" fn(usize, usize, u32, *mut u32) -> i32>(PROTECT_ORIG.load(Ordering::Acquire))(addr, size, new, old);
    crate::epoch::bump_range(lo, hi);
    r
}

fn rule(_dll: &[u8], name: Option<&[u8]>, value: usize) -> Option<usize> {
    single_rule(&[b"UnmapViewOfFile"], &UNMAP_ORIG, unmap_w as *const () as usize, name, value)
        .or_else(|| single_rule(&[b"UnmapViewOfFileEx"], &UNMAPEX_ORIG, unmapex_w as *const () as usize, name, value))
        .or_else(|| single_rule(&[b"UnmapViewOfFile2"], &UNMAP2_ORIG, unmap2_w as *const () as usize, name, value))
        .or_else(|| single_rule(&[b"VirtualProtect"], &PROTECT_ORIG, protect_w as *const () as usize, name, value))
}

pub fn install() {
    static DONE: AtomicU32 = AtomicU32::new(0);
    if DONE.swap(1, Ordering::AcqRel) == 0 {
        let n = iat::patch_all(&rule);
        record(M, "TomboxAddressSpaceEpoch", &format!("UnmapViewOfFile/UnmapViewOfFileEx/UnmapViewOfFile2/VirtualProtect imports log their ranges to the address-space change log: {n} slots"));
    }
}
