use core::ffi::c_void;

use crate::sys::*;

#[link(name = "kernel32")]
extern "system" {
    fn VirtualProtect(addr: *mut c_void, size: usize, new: u32, old: *mut u32) -> i32;
    fn K32EnumProcessModules(process: Handle, modules: *mut usize, cb: u32, needed: *mut u32) -> i32;
    fn GetModuleFileNameW(module: usize, out: *mut u16, len: u32) -> u32;
    fn GetWindowsDirectoryW(out: *mut u16, len: u32) -> u32;
    fn GetModuleHandleExW(flags: u32, name: *const u16, out: *mut usize) -> i32;
}

const MEM_PRIVATE: u32 = 0x2_0000;
const READABLE: u32 = 0x02 | 0x04 | 0x08 | 0x20 | 0x40 | 0x80;

const COMPAT_MODULES: &[&str] = &[
    "xmem", "xg_xs", "xg_x", "d3d12_x", "d3d12_xs", "dxcompiler_x", "dxcompiler_xs", "xapu", "xma", "acphal",
    "audiohalwrapper", "chad", "mfplat", "xcrdapi", "xcurl", "xfrontpaneldisplay", "xbthunk",
];

fn query(addr: usize) -> Option<MemoryBasicInformation> {
    let mut m = MemoryBasicInformation::default();
    let n = unsafe { VirtualQueryEx(GetCurrentProcess(), addr as *const c_void, &mut m, core::mem::size_of::<MemoryBasicInformation>()) };
    if n == 0 { None } else { Some(m) }
}

fn readable(addr: usize, len: usize) -> bool {
    let end = match addr.checked_add(len) {
        Some(e) => e,
        None => return false,
    };
    let mut p = addr;
    while p < end {
        let m = match query(p) {
            Some(m) => m,
            None => return false,
        };
        if m.state != MEM_COMMIT || m.protect & READABLE == 0 || m.protect & PAGE_GUARD != 0 {
            return false;
        }
        p = m.base + m.region_size;
    }
    true
}

unsafe fn rd32(a: usize) -> u32 {
    core::ptr::read_unaligned(a as *const u32)
}

unsafe fn rd64(a: usize) -> u64 {
    core::ptr::read_unaligned(a as *const u64)
}

unsafe fn cstr<'a>(a: usize, limit: usize) -> Option<&'a [u8]> {
    if a >= limit {
        return None;
    }
    let max = (limit - a).min(512);
    let s = core::slice::from_raw_parts(a as *const u8, max);
    s.iter().position(|&c| c == 0).map(|n| &s[..n])
}

pub fn image_size(base: usize) -> Option<usize> {
    unsafe {
        if !readable(base, 0x400) || core::ptr::read_unaligned(base as *const u16) != 0x5a4d {
            return None;
        }
        let lfanew = rd32(base + 0x3c) as usize;
        if lfanew > 0x1000 || !readable(base + lfanew, 0x108) || rd32(base + lfanew) != 0x4550 {
            return None;
        }
        let opt = base + lfanew + 24;
        if core::ptr::read_unaligned(opt as *const u16) != 0x20b {
            return None;
        }
        Some(rd32(opt + 56) as usize)
    }
}

unsafe fn write_slot(slot: usize, value: usize) -> bool {
    let mut old = 0u32;
    if VirtualProtect(slot as *mut c_void, 8, PAGE_READWRITE, &mut old) == 0 {
        return false;
    }
    core::ptr::write_volatile(slot as *mut usize, value);
    let mut tmp = 0u32;
    VirtualProtect(slot as *mut c_void, 8, old, &mut tmp);
    true
}

pub type Rule<'a> = &'a dyn Fn(&[u8], Option<&[u8]>, usize) -> Option<usize>;

pub unsafe fn patch_module(base: usize, rule: Rule) -> usize {
    let size = match image_size(base) {
        Some(s) => s,
        None => return 0,
    };
    let limit = base + size;
    let lfanew = rd32(base + 0x3c) as usize;
    let dirs = base + lfanew + 24 + 112;
    let mut patched = 0;
    let (irva, isz) = (rd32(dirs + 8) as usize, rd32(dirs + 12) as usize);
    if irva != 0 && isz != 0 && irva < size {
        let mut d = base + irva;
        while d + 20 <= limit && readable(d, 20) {
            let (oft, name, ft) = (rd32(d) as usize, rd32(d + 12) as usize, rd32(d + 16) as usize);
            if name == 0 && ft == 0 {
                break;
            }
            let dll = cstr(base + name, limit).unwrap_or(b"");
            let int = if oft != 0 { oft } else { ft };
            let mut i = 0usize;
            loop {
                let ent_at = base + int + i * 8;
                let slot = base + ft + i * 8;
                if slot + 8 > limit || ent_at + 8 > limit || !readable(slot, 8) || !readable(ent_at, 8) {
                    break;
                }
                let ent = rd64(ent_at);
                let value = rd64(slot) as usize;
                if ent == 0 || value == 0 {
                    break;
                }
                let fname = if oft != 0 && ent & (1 << 63) == 0 { cstr(base + (ent as usize & 0x7fff_ffff) + 2, limit) } else { None };
                if let Some(new) = rule(dll, fname, value) {
                    if new != value && write_slot(slot, new) {
                        patched += 1;
                    }
                }
                i += 1;
            }
            d += 20;
        }
    }
    let (drva, dsz) = (rd32(dirs + 13 * 8) as usize, rd32(dirs + 13 * 8 + 4) as usize);
    if drva != 0 && dsz != 0 && drva < size {
        let mut d = base + drva;
        while d + 32 <= limit && readable(d, 32) {
            let (name, iat, int) = (rd32(d + 4) as usize, rd32(d + 12) as usize, rd32(d + 16) as usize);
            if name == 0 || iat == 0 {
                break;
            }
            let dll = cstr(base + name, limit).unwrap_or(b"");
            let mut i = 0usize;
            loop {
                let slot = base + iat + i * 8;
                if slot + 8 > limit || !readable(slot, 8) {
                    break;
                }
                let value = rd64(slot) as usize;
                if value == 0 {
                    break;
                }
                let ent = if int != 0 && readable(base + int + i * 8, 8) { rd64(base + int + i * 8) } else { 0 };
                let fname = if ent != 0 && ent & (1 << 63) == 0 { cstr(base + (ent as usize & 0x7fff_ffff) + 2, limit) } else { None };
                if (value < base || value >= limit) && rule(dll, fname, value).map(|new| new != value && write_slot(slot, new)).unwrap_or(false) {
                    patched += 1;
                }
                i += 1;
            }
            d += 32;
        }
    }
    patched
}

fn own_module() -> usize {
    let mut h = 0usize;
    unsafe { GetModuleHandleExW(6, own_module as *const u16, &mut h) };
    h
}

fn lower(w: &[u16]) -> String {
    String::from_utf16_lossy(w).to_lowercase().replace('/', "\\")
}

fn is_title_module(h: usize, windir: &str) -> bool {
    if h == own_module() {
        return false;
    }
    let mut buf = [0u16; 1024];
    let n = unsafe { GetModuleFileNameW(h, buf.as_mut_ptr(), buf.len() as u32) } as usize;
    if n == 0 {
        return false;
    }
    let path = lower(&buf[..n]);
    if !windir.is_empty() && path.starts_with(windir) {
        return false;
    }
    let file = path.rsplit('\\').next().unwrap_or("");
    let stem = file.strip_suffix(".dll").or_else(|| file.strip_suffix(".exe")).unwrap_or(file);
    if stem.starts_with("api-ms-") || stem.starts_with("ext-ms-") || stem.contains("tombox") {
        return false;
    }
    !COMPAT_MODULES.contains(&stem)
}

pub fn title_modules() -> Vec<usize> {
    let mut wd = [0u16; 512];
    let n = unsafe { GetWindowsDirectoryW(wd.as_mut_ptr(), wd.len() as u32) } as usize;
    let mut windir = lower(&wd[..n.min(wd.len())]);
    if !windir.is_empty() && !windir.ends_with('\\') {
        windir.push('\\');
    }
    let mut mods = vec![0usize; 1024];
    let mut needed = 0u32;
    let mut out = Vec::new();
    unsafe {
        if K32EnumProcessModules(GetCurrentProcess(), mods.as_mut_ptr(), (mods.len() * 8) as u32, &mut needed) != 0 {
            let count = (needed as usize / 8).min(mods.len());
            for &h in &mods[..count] {
                if is_title_module(h, &windir) {
                    out.push(h);
                }
            }
        }
    }
    let mut p = 0x1_0000usize;
    while p < 0x7fff_ffff_0000 {
        let m = match query(p) {
            Some(m) => m,
            None => break,
        };
        if m.state == MEM_COMMIT && m.kind == MEM_PRIVATE && m.base == m.alloc_base && m.protect & READABLE != 0 && m.protect & PAGE_GUARD == 0 && image_size(m.base).is_some() {
            out.push(m.base);
        }
        let next = m.base + m.region_size;
        if next <= p {
            break;
        }
        p = next;
    }
    out
}

pub fn patch_all(rule: Rule) -> usize {
    title_modules().into_iter().map(|b| unsafe { patch_module(b, rule) }).sum()
}

#[cfg(test)]
mod tests {
    use super::*;

    extern "system" {
        fn GetModuleHandleW(name: *const u16) -> usize;
    }

    #[test]
    fn walks_own_import_table() {
        let exe = unsafe { GetModuleHandleW(core::ptr::null()) };
        assert!(image_size(exe).is_some());
        let seen = std::cell::RefCell::new(Vec::new());
        let rule = |dll: &[u8], name: Option<&[u8]>, _v: usize| -> Option<usize> {
            seen.borrow_mut().push((dll.to_ascii_lowercase(), name.map(|n| n.to_vec())));
            None
        };
        assert_eq!(unsafe { patch_module(exe, &rule) }, 0);
        assert!(seen.borrow().iter().any(|(d, n)| d.starts_with(b"kernel32") && n.as_deref() == Some(&b"VirtualAlloc"[..])));
        assert!(!title_modules().contains(&own_module()) || own_module() == exe);
    }
}
