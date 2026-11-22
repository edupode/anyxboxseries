use core::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

const RING: usize = 16384;

struct Log {
    ranges: Vec<(usize, usize)>,
}

static SEQ: AtomicU64 = AtomicU64::new(1);
static LOG: Mutex<Log> = Mutex::new(Log { ranges: Vec::new() });

pub fn bump_range(lo: usize, hi: usize) {
    let mut g = LOG.lock().unwrap_or_else(|p| p.into_inner());
    if g.ranges.len() < RING {
        g.ranges.resize(RING, (0, 0));
    }
    let seq = SEQ.load(Ordering::Acquire);
    g.ranges[(seq as usize) % RING] = (lo, hi.max(lo + 1));
    SEQ.store(seq + 1, Ordering::Release);
}

#[repr(C)]
#[derive(Default)]
struct Mbi {
    base: usize,
    alloc_base: usize,
    alloc_protect: u32,
    partition: u32,
    region_size: usize,
    state: u32,
    protect: u32,
    ty: u32,
}

extern "system" {
    fn VirtualQuery(addr: *const core::ffi::c_void, mbi: *mut Mbi, len: usize) -> usize;
}

pub fn bump_free(addr: usize, size: usize) -> (usize, usize) {
    if size != 0 {
        let hi = addr.saturating_add(size);
        bump_range(addr, hi);
        return (addr, hi);
    }
    if let Some(n) = crate::phys::region_extent(addr) {
        let hi = addr.saturating_add(n);
        bump_range(addr, hi);
        return (addr, hi);
    }
    let mut end = addr.saturating_add(1);
    let mut p = addr;
    for _ in 0..1_000_000 {
        let mut m = Mbi::default();
        if unsafe { VirtualQuery(p as *const _, &mut m, core::mem::size_of::<Mbi>()) } == 0 || m.alloc_base != addr || m.state == 0x1_0000 {
            break;
        }
        end = m.base + m.region_size;
        p = end;
    }
    bump_range(addr, end);
    (addr, end)
}

pub fn changed_since(seq: u64, lo: usize, hi: usize) -> bool {
    let g = LOG.lock().unwrap_or_else(|p| p.into_inner());
    let now = SEQ.load(Ordering::Acquire);
    if seq >= now {
        return false;
    }
    if now - seq > RING as u64 || g.ranges.is_empty() {
        return true;
    }
    (seq..now).any(|s| {
        let (a, b) = g.ranges[(s as usize) % RING];
        a < hi && lo < b
    })
}

#[no_mangle]
pub extern "system" fn TomboxAddressSpaceEpoch() -> u64 {
    crate::range_hooks::install();
    SEQ.load(Ordering::Acquire)
}

#[no_mangle]
pub extern "system" fn TomboxAddressSpaceChangedSince(seq: u64, lo: usize, hi: usize) -> i32 {
    changed_since(seq, lo, hi) as i32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reports_only_overlapping_changes() {
        let s0 = TomboxAddressSpaceEpoch();
        bump_range(0x10_0000, 0x20_0000);
        assert!(changed_since(s0, 0x18_0000, 0x19_0000));
        assert!(!changed_since(s0, 0x30_0000, 0x40_0000));
        let s1 = TomboxAddressSpaceEpoch();
        assert!(!changed_since(s1, 0x10_0000, 0x20_0000));
    }
}
