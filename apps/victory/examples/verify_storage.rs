use std::ffi::{c_char, c_void};
use std::io::{Read, Seek, SeekFrom};
use std::os::windows::io::FromRawHandle;
use std::path::PathBuf;
use std::ptr::{null, null_mut};
use std::time::{Duration, Instant};
use victory_hle::modules::{dstorage, files, xgameruntime::Guid};

type Obj = *mut c_void;
type Out = *mut Obj;
const REL: &str = r"data\ttdb\test.ttpf";
const GUEST: &str = r"G:\.\data\ttdb\test.ttpf";
const FACTORY: Guid = Guid::new(
    0x62959d11,
    0xd45f,
    0x4884,
    [0xa0, 0xa3, 0xf7, 0x86, 0x2e, 0xa2, 0x43, 0xae],
);
const FILE: Guid = Guid::new(
    0x43704f9f,
    0x27c6,
    0x4a9f,
    [0x88, 0x15, 0xd1, 0x65, 0x27, 0x89, 0x4d, 0xf4],
);

#[repr(C)]
struct QueueDesc {
    source: u64,
    capacity: u16,
    priority: i8,
    name: *const c_char,
    version: u64,
}
#[repr(C)]
struct Request {
    options: u64,
    dest: Obj,
    page_offset: u64,
    bytes: u32,
    file: Obj,
    offset: u64,
    source_bytes: u32,
    intermediate_bytes: u32,
    tag: u64,
    name: *const c_char,
}

fn slot<T: Copy>(object: Obj, slot: usize) -> T {
    assert!(!object.is_null() && std::mem::size_of::<T>() == std::mem::size_of::<Obj>());
    // SAFETY: calls below use the public console ABI, matching each slot's signature.
    unsafe { std::mem::transmute_copy(&(*object.cast::<*const Obj>()).add(slot).read()) }
}

fn expected(offset: u64) -> [u8; 16] {
    std::array::from_fn(|i| (((offset + i as u64) * 31 + 17) & 255) as u8)
}

fn main() {
    let view = PathBuf::from(std::env::args_os().nth(1).expect("synthetic view"));
    files::attach_archive_mount(&view).unwrap();
    files::mount_at('G', &view);
    let wide = |s: &str| s.encode_utf16().chain([0]).collect::<Vec<_>>();
    let mut factory = null_mut();
    assert_eq!(dstorage::DStorageGetFactory(&FACTORY, &mut factory), 0);
    let open: extern "system" fn(Obj, *const u16, *const Guid, Out) -> i32 = slot(factory, 4);
    let mut file_a = null_mut();
    let mut file_b = null_mut();
    assert_eq!(open(factory, wide(GUEST).as_ptr(), &FILE, &mut file_a), 0);
    assert_eq!(open(factory, wide(GUEST).as_ptr(), &FILE, &mut file_b), 0);
    let denied_parent = |path: &str| {
        let mut out = null_mut();
        assert_eq!(
            open(factory, wide(path).as_ptr(), &FILE, &mut out) as u32,
            0x80070005
        );
        assert!(out.is_null());
    };
    denied_parent(r"G:\data\child\..\ttdb\test.ttpf");
    denied_parent(&format!(r"G:\data\{}\..\ttdb\test.ttpf", "q".repeat(249)));
    let mut missing = null_mut();
    assert_eq!(
        open(
            factory,
            wide(r"G:\.\data\ttdb\unlisted.ttpf").as_ptr(),
            &FILE,
            &mut missing
        ) as u32,
        0x80070002
    );
    assert!(missing.is_null());
    let win_open =
        |access| files::CreateFileW(wide(GUEST).as_ptr(), access, 1, null(), 3, 0, null_mut());
    assert_eq!(win_open(0x40000000) as isize, -1);
    assert_eq!(std::io::Error::last_os_error().raw_os_error(), Some(5));
    assert_eq!(
        std::fs::File::open(view.join(REL))
            .unwrap_err()
            .raw_os_error(),
        Some(5)
    );
    let win_read = || {
        let handle = win_open(0x80000000);
        assert_ne!(handle as isize, -1);
        // SAFETY: a successful CreateFileW transfers this handle to the caller.
        unsafe { std::fs::File::from_raw_handle(handle) }
    };
    let mut first = win_read();
    let mut second = win_read();
    first.seek(SeekFrom::Start(257)).unwrap();
    let mut check = [0; 16];
    first.read_exact(&mut check).unwrap();
    assert_eq!(check, expected(257));
    assert_eq!(second.stream_position().unwrap(), 0);
    let create_q: extern "system" fn(Obj, *const QueueDesc, *const Guid, Out) -> i32 =
        slot(factory, 10);
    let create_s: extern "system" fn(Obj, u32, *const c_char, *const Guid, Out) -> i32 =
        slot(factory, 5);
    let desc = QueueDesc {
        source: 0,
        capacity: 128,
        priority: 0,
        name: c"range check".as_ptr(),
        version: 1,
    };
    let mut queue = null_mut();
    let mut status = null_mut();
    assert_eq!(create_q(factory, &desc, null(), &mut queue), 0);
    assert_eq!(
        create_s(factory, 1, c"completion".as_ptr(), null(), &mut status),
        0
    );
    let enqueue: extern "system" fn(Obj, *const Request) = slot(queue, 3);
    let mark: extern "system" fn(Obj, Obj, u32) = slot(queue, 4);
    let submit: extern "system" fn(Obj) = slot(queue, 6);
    let done: extern "system" fn(Obj, u32) -> bool = slot(status, 3);
    let result: extern "system" fn(Obj, u32) -> i32 = slot(status, 4);
    let offsets = [4097, 263, 855];
    let mut readbacks = [[0u8; 16]; 3];
    for ((dest, file), offset) in readbacks
        .iter_mut()
        .zip([file_a, file_b, file_a])
        .zip(offsets)
    {
        let req = Request {
            options: 0,
            dest: dest.as_mut_ptr().cast(),
            page_offset: 0,
            bytes: 16,
            file,
            offset,
            source_bytes: 16,
            intermediate_bytes: 0,
            tag: 0,
            name: null(),
        };
        enqueue(queue, &req);
    }
    mark(queue, status, 0);
    assert_eq!(result(status, 0) as u32, 0x8000000a);
    submit(queue);
    let start = Instant::now();
    while !done(status, 0) {
        assert!(
            start.elapsed() < Duration::from_secs(10),
            "DirectStorage completion timed out"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
    assert_eq!(result(status, 0), 0);
    assert_eq!(readbacks, offsets.map(expected));
    let close: extern "system" fn(Obj) = slot(queue, 8);
    close(queue);
    for object in [queue, status, file_a, file_b] {
        let release: extern "system" fn(Obj) -> u32 = slot(object, 2);
        release(object);
    }
    println!(
        "DSTORAGE CHECK PASSED: two actual file objects, three matching queued ranges, independent Win32 cursors, traversal and unlisted names rejected, write and raw placeholder reads denied"
    );
}
