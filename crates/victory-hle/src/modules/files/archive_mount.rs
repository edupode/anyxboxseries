//! Read-only archive capability for the local Windows comparison launcher.
//! The desktop chooses the archive IDs and authenticates the child process.

use std::collections::HashMap;
use std::fs::File;
use std::io::{self, Read, Write};
use std::os::windows::io::FromRawHandle;
use std::path::Path;
use std::sync::{Mutex, OnceLock};
use windows_sys::Win32::Foundation::{HANDLE, HANDLE_FLAG_INHERIT, INVALID_HANDLE_VALUE, SetHandleInformation, SetLastError};
use windows_sys::Win32::Storage::FileSystem::{FILE_TYPE_DISK, FILE_TYPE_PIPE, GetFileType};

struct Mount {
    prefix: String,
    ids: HashMap<String, u32>,
    connection: Mutex<Option<(File, File)>>,
}

static MOUNT: OnceLock<Mount> = OnceLock::new();

fn object_path(text: &str) -> String {
    text.strip_prefix(r"\\?\").unwrap_or(text).replace('\\', "/").to_lowercase()
}

fn invalid(msg: &'static str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, msg)
}

fn transport(name: &str) -> io::Result<HANDLE> {
    let value = std::env::var(name).ok().and_then(|s| s.parse::<usize>().ok())
        .ok_or_else(|| invalid("missing archive pipe"))?;
    let handle = value as HANDLE;
    if handle.is_null() || handle == INVALID_HANDLE_VALUE || unsafe { GetFileType(handle) } != FILE_TYPE_PIPE {
        return Err(invalid("invalid archive pipe"));
    }
    Ok(handle)
}

pub fn initialize(root: &Path) -> io::Result<()> {
    let manifest = std::env::var_os("XCL_ARCHIVE_CATALOG");
    if manifest.is_none() && std::env::var_os("XCL_ARCHIVE_REQUEST_HANDLE").is_none()
        && std::env::var_os("XCL_ARCHIVE_REPLY_HANDLE").is_none() {
        return Ok(());
    }
    let manifest = manifest.ok_or_else(|| invalid("missing archive catalog"))?;
    let path = Path::new(&manifest);
    if !path.is_absolute() { return Err(invalid("archive catalog must be absolute")); }
    let mut text = String::new();
    const LIMIT: usize = 8 * 1024 * 1024;
    File::open(path)?.take((LIMIT+1) as u64).read_to_string(&mut text)?;
    if text.len() > LIMIT { return Err(invalid("oversize archive catalog")); }
    // The desktop serializes its validated Windows names with forward-slash
    // separators. Its canonical JSON has no escaped characters or whitespace.
    let items = text.strip_prefix("[\"").and_then(|s| s.strip_suffix("\"]"))
        .ok_or_else(|| invalid("invalid archive catalog"))?;
    let mut ids = HashMap::new();
    for (index, name) in items.split("\",\"").enumerate() {
        let name = name.to_lowercase();
        if index >= 65536 || !(name.ends_with(".rpf") || name.ends_with(".ttpf")) ||
            name.split('/').any(|p| p.is_empty() || p.ends_with(['.',' '])
                || p.chars().any(|c| c.is_control() || "\\:\"<>|?*".contains(c))) ||
            ids.insert(name, index as u32).is_some() {
            return Err(invalid("invalid archive catalog entry"));
        }
    }
    let absolute = std::path::absolute(root)?;
    let mut prefix = object_path(absolute.to_str().ok_or_else(|| invalid("invalid game view path"))?);
    if !prefix.ends_with('/') { prefix.push('/'); }
    let request = transport("XCL_ARCHIVE_REQUEST_HANDLE")?;
    let reply = transport("XCL_ARCHIVE_REPLY_HANDLE")?;
    if request == reply { return Err(invalid("duplicate archive pipe")); }
    // SAFETY: these are distinct pipe endpoints inherited from the launch job.
    let connection = unsafe {
        if SetHandleInformation(request,HANDLE_FLAG_INHERIT,0) == 0 ||
            SetHandleInformation(reply,HANDLE_FLAG_INHERIT,0) == 0 { return Err(io::Error::last_os_error()); }
        (File::from_raw_handle(request), File::from_raw_handle(reply))
    };
    MOUNT.set(Mount{prefix,ids,connection:Mutex::new(Some(connection))})
        .map_err(|_| invalid("archive mount already attached"))
}

fn exchange(pipes: &mut (File,File), fields: [u32;6]) -> io::Result<(u32,HANDLE)> {
    let mut frame = [0u8;24];
    for (dst, src) in frame.chunks_exact_mut(4).zip(fields) { dst.copy_from_slice(&src.to_le_bytes()); }
    pipes.0.write_all(&frame)?;
    let mut reply = [0u8;16];
    pipes.1.read_exact(&mut reply)?;
    let status = u32::from_le_bytes(reply[..4].try_into().unwrap());
    let value = u64::from_le_bytes(reply[8..].try_into().unwrap()) as usize as HANDLE;
    if reply[4..8] != [0;4] || (status != 0 && !value.is_null())
        || (status == 0 && (value.is_null() || value == INVALID_HANDLE_VALUE || unsafe { GetFileType(value) } != FILE_TYPE_DISK)) {
        return Err(invalid("invalid archive reply"));
    }
    Ok((status,value))
}

pub(super) fn open(path: &[u16], access: u32, share: u32, disposition: u32,
                   flags: u32, ordinary: bool) -> Option<HANDLE> {
    let mount = MOUNT.get()?;
    let text = String::from_utf16(path.strip_suffix(&[0]).unwrap_or(path)).ok()?;
    let host = object_path(&text);
    let relative = host.strip_prefix(&mount.prefix)?;
    if !(relative.ends_with(".rpf") || relative.ends_with(".ttpf")) { return None; }
    let denied = |code| { unsafe { SetLastError(code) }; INVALID_HANDLE_VALUE };
    if !ordinary || access & !0x8012_0089 != 0 || share & !7 != 0
        || !matches!(disposition,3|4) || flags & !0x7a00_00a1 != 0 { return Some(denied(5)); }
    // A current-directory component names the same entry. All other spellings
    // still need to exactly match a manifest key, including its directory.
    let key = relative.split('/').filter(|part| *part != ".").collect::<Vec<_>>().join("/");
    let Some(&id) = mount.ids.get(&key) else { return Some(denied(2)); };
    let mut connection = mount.connection.lock().unwrap_or_else(|p| p.into_inner());
    let Some(pipes) = connection.as_mut() else { return Some(denied(109)); };
    match exchange(pipes,[0x314c4358,std::process::id(),id,access,share,flags]) {
        Ok((0,handle)) => {
            unsafe { SetLastError(if disposition == 4 { 183 } else { 0 }) };
            Some(handle)
        }
        Ok((status,_)) => Some(denied(status)),
        Err(_) => { *connection=None; Some(denied(109)) }
    }
}
