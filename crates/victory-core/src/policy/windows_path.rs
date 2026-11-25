use std::ffi::{OsString, c_void};
use std::fs::{File, OpenOptions};
use std::io;
use std::os::windows::{ffi::OsStringExt, fs::OpenOptionsExt, io::AsRawHandle};
use std::path::{Component, Path, PathBuf, Prefix};

#[link(name = "kernel32")]
unsafe extern "system" {
    fn GetFinalPathNameByHandleW(
        file: *mut c_void,
        output: *mut u16,
        capacity: u32,
        flags: u32,
    ) -> u32;
}

fn metadata_handle(path: &Path) -> io::Result<File> {
    // FILE_READ_ATTRIBUTES and FILE_FLAG_BACKUP_SEMANTICS work for files and
    // directories. Follow reparse points so that the check sees the target.
    OpenOptions::new()
        .access_mode(0x80)
        .custom_flags(0x0200_0000)
        .open(path)
}

fn final_name(file: &File, volume: u32) -> io::Result<Vec<u16>> {
    let mut name = vec![0u16; 256];
    loop {
        // SAFETY: the owned handle and writable buffer are live for this call.
        let n = unsafe {
            GetFinalPathNameByHandleW(
                file.as_raw_handle(),
                name.as_mut_ptr(),
                name.len() as u32,
                volume,
            )
        } as usize;
        if n == 0 {
            return Err(io::Error::last_os_error());
        }
        if n < name.len() {
            name.truncate(n);
            return Ok(name);
        }
        if n > 32_768 {
            return Err(io::Error::other(
                "final path exceeds the Windows path limit",
            ));
        }
        name.resize(n.max(name.len() + 1), 0);
    }
}

/// The DOS volume-name query can be denied to an AppContainer even when it
/// owns the file. Accept a reconstructed DOS path only if two live handles
/// yield the same complete normalized NT device path, including the volume.
pub(super) fn canonical_local(path: &Path) -> io::Result<PathBuf> {
    let denied = || io::Error::from(io::ErrorKind::PermissionDenied);
    let mut parts = path.components();
    let Some(Component::Prefix(prefix)) = parts.next() else {
        return Err(denied());
    };
    let (Prefix::Disk(drive) | Prefix::VerbatimDisk(drive)) = prefix.kind() else {
        return Err(denied());
    };
    if parts.next() != Some(Component::RootDir) {
        return Err(denied());
    }

    let original = metadata_handle(path)?;
    let relative = final_name(&original, 4)?; // FILE_NAME_NORMALIZED | VOLUME_NAME_NONE
    let slash = u16::from(b'\\');
    if relative.first() != Some(&slash) || relative.get(1) == Some(&slash) {
        return Err(denied());
    }
    let mut dos: Vec<u16> = r"\\?\".encode_utf16().collect();
    dos.extend([u16::from(drive), u16::from(b':')]);
    dos.extend(relative);
    let candidate = PathBuf::from(OsString::from_wide(&dos));
    let reopened = metadata_handle(&candidate)?;
    if final_name(&original, 2)? != final_name(&reopened, 2)? {
        // VOLUME_NAME_NT
        return Err(denied());
    }
    Ok(candidate)
}
