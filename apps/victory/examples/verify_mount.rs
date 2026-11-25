use std::ffi::OsStr;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::windows::ffi::OsStrExt;
use std::os::windows::io::FromRawHandle;
use std::path::PathBuf;

fn main() {
    use victory_hle::modules::files;
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    assert_eq!(args.len(), 4);
    let view = PathBuf::from(&args[0]);
    files::attach_archive_mount(&view).unwrap();
    files::mount_at('G', &view);
    let wide = |s: &OsStr| s.encode_wide().chain([0]).collect::<Vec<u16>>();
    let path = wide(OsStr::new(&format!("G:\\{}", args[1].to_str().unwrap())));
    let access = |rights| files::CreateFileW(path.as_ptr(), rights, 1, std::ptr::null(), 3, 0, std::ptr::null_mut());
    let failed = -1isize as *mut std::ffi::c_void;
    assert_eq!(access(0x40000000),failed);
    assert_eq!(std::io::Error::last_os_error().raw_os_error(),Some(5));
    let open = || {
        let h=access(0x80000000);
        assert_ne!(h,failed,"read-only archive open: {}", std::io::Error::last_os_error());
        unsafe { File::from_raw_handle(h) }
    };
    let mut first = open();
    let mut second = open();
    let offset = args[2].to_str().unwrap().parse::<u64>().unwrap();
    let hex = args[3].to_str().unwrap().as_bytes();
    assert_eq!(hex.len(),32);
    let expected: Vec<_> = hex.chunks_exact(2).map(|b| u8::from_str_radix(std::str::from_utf8(b).unwrap(),16).unwrap()).collect();
    let mut actual = [0;16];
    first.seek(SeekFrom::Start(offset)).unwrap();
    first.read_exact(&mut actual).unwrap();
    assert_eq!(actual.as_slice(),expected);
    assert_eq!(second.stream_position().unwrap(),0);
    if args[1] == "comparison-probe.rpf" {
        assert_eq!(second.write(&[0]).unwrap_err().raw_os_error(),Some(5));
    }
    assert!(std::fs::File::open(view.join(&args[1])).is_err());
    println!("ARCHIVE CHECK PASSED: independent handles; 16 bytes matched; write-open and unbrokered view read denied");
}
