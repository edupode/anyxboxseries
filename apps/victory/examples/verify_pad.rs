use std::ffi::c_void;
use std::time::{Duration, Instant};
use victory_hle::modules::gameinput;

type Object = *const c_void;
#[repr(C)]
#[derive(Default)]
struct Pad {
    buttons: u32,
    lt: f32,
    rt: f32,
    lx: f32,
    ly: f32,
    rx: f32,
    ry: f32,
}

fn method<T: Copy>(obj: Object, index: usize) -> T {
    assert!(!obj.is_null());
    // SAFETY: object and reading have the documented console GameInput ABI.
    unsafe { std::mem::transmute_copy(&(*obj.cast::<*const Object>()).add(index).read()) }
}

fn main() {
    victory_core::logging::init("info");
    let mut input = std::ptr::null();
    assert_eq!(gameinput::GameInputCreate(&mut input), 0);
    let input = input.cast();
    let current: extern "system" fn(Object, u32, Object, *mut Object) -> i32 = method(input, 4);
    let read = || {
        let mut obj = std::ptr::null();
        assert_eq!(current(input, 0x40000, std::ptr::null(), &mut obj), 0);
        let state: extern "system" fn(Object, *mut Pad) -> bool = method(obj, 0xb0 / 8);
        let sequence: extern "system" fn(Object, u32) -> u64 = method(obj, 4);
        let release: extern "system" fn(Object) -> u32 = method(obj, 2);
        let mut pad = Pad::default();
        assert!(state(obj, &mut pad));
        let n = sequence(obj, 0x40000);
        release(obj);
        (pad, n)
    };
    let mut last = read().1;
    let wait = |cond: fn(&Pad) -> bool, previous: u64| {
        let start = Instant::now();
        loop {
            let (pad, sequence) = read();
            if sequence > previous && cond(&pad) {
                return sequence;
            }
            assert!(
                start.elapsed() < Duration::from_secs(15),
                "no expected GameInput reading"
            );
            std::thread::sleep(Duration::from_millis(4));
        }
    };
    println!("PAD FIXTURE READY");
    last = wait(|p| p.buttons & 4 != 0, last);
    println!("PAD A DOWN {last}");
    last = wait(|p| p.buttons & 4 == 0, last);
    println!("PAD A UP {last}");
    last = wait(|p| p.ly == 1., last);
    println!("PAD STICK DOWN {last}");
    last = wait(|p| p.ly == 0., last);
    println!(
        "PAD FIXTURE PASSED genuine A and left-stick press and release through reading {last}"
    );
}
