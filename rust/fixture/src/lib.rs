// The Rust fixture: prog.c's ten checks, in Rust, for 68k ELF with core
// only (no_std). tests/test_openelf.sh loads it with elf.library's
// core and runs it.
// Copyright (c) 2026 AmiCradle Team. MIT.
#![no_std]

extern crate alloc;

use alloc::format;
use alloc::vec::Vec;
use core::alloc::{GlobalAlloc, Layout};
use core::sync::atomic::{AtomicI32, AtomicUsize, Ordering};

/// A bump allocator on a static block: enough for format! and Vec, and it
/// puts the loader's .bss placement under test too.
struct Bump;
static mut HEAP: [u8; 16384] = [0; 16384];
static NEXT: AtomicUsize = AtomicUsize::new(0);
unsafe impl GlobalAlloc for Bump {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        let base = &raw mut HEAP as *mut u8 as usize;
        let start = (base + NEXT.load(Ordering::Relaxed) + l.align() - 1) & !(l.align() - 1);
        if start + l.size() > base + 16384 {
            return core::ptr::null_mut();
        }
        NEXT.store(start + l.size() - base, Ordering::Relaxed);
        start as *mut u8
    }
    unsafe fn dealloc(&self, _: *mut u8, _: Layout) {}
}
#[global_allocator]
static ALLOC: Bump = Bump;

#[repr(C)]
pub struct FxApi {
    hello: extern "C" fn(*const u8) -> i32,
    version: i32,
}

extern "C" {
    fn host_add(a: i32, b: i32) -> i32;     // the loader's resolve answers it
}

static COUNTER: AtomicI32 = AtomicI32::new(0);              // .bss
static mut COMMON_VAR: i32 = 0;
#[no_mangle]
pub static NAMES: [&str; 3] = ["one", "two", "three"];      // addresses in data
fn sq(x: i32) -> i32 { x * x }
fn cube(x: i32) -> i32 { x * x * x }
#[no_mangle]
pub static OPS: [fn(i32) -> i32; 2] = [sq, cube];           // function pointers in data
#[no_mangle]
pub static mut BIG: [u8; 3000] = { let mut b = [0u8; 3000]; b[0] = 1; b[1] = 2; b[2] = 3; b };
#[no_mangle]
pub static TEXT: &[u8] = b"relocated\0";

#[inline(never)]
fn far_away(x: i32) -> i32 { x + 1000 }

#[inline(never)]
fn len(s: &[u8]) -> i32 {
    let mut n = 0;
    while s[n] != 0 { n += 1; }
    n as i32
}

#[no_mangle]
pub extern "C" fn _start(api: &FxApi) -> i32 {
    let mut ok = 0;
    let names = core::hint::black_box(&NAMES);
    if names[1].as_bytes()[0] == b't' && names[2].as_bytes()[4] == b'e' { ok |= 1; }
    let ops = core::hint::black_box(&OPS);
    if ops[0](3) == 9 && ops[1](3) == 27 { ok |= 2; }
    if COUNTER.load(Ordering::Relaxed) == 0 {
        COUNTER.fetch_add(1, Ordering::Relaxed);
        COUNTER.fetch_add(1, Ordering::Relaxed);
        if COUNTER.load(Ordering::Relaxed) == 2 { ok |= 4; }
    }
    unsafe {
        let c = &raw mut COMMON_VAR;
        if c.read_volatile() == 0 { c.write_volatile(5); if c.read_volatile() == 5 { ok |= 8; } }
    }
    if (api.hello)(b"ELF\0".as_ptr()) == 43 { ok |= 16; }
    if len(core::hint::black_box(TEXT)) == 9 { ok |= 32; }
    unsafe {
        let b = &raw const BIG;
        if (*b)[0] == 1 && (*b)[2] == 3 && (*b)[2999] == 0 { ok |= 64; }
    }
    if far_away(core::hint::black_box(1)) == 1001 { ok |= 128; }
    if unsafe { host_add(2, 3) } == 5 { ok |= 256; }
    // alloc and core::fmt with strings. Not integers: LLVM's m68k backend
    // (rustc 1.97) miscompiles core's integer formatting, with or without a
    // loader (README.md, "Rust")
    let squares: Vec<u32> = (1..=4).map(|n| n * n).collect();
    let s = format!("{}-{}", "elf", "ok");
    if api.version == 1 && squares.iter().sum::<u32>() == 30 && s.as_bytes() == b"elf-ok" { ok |= 512; }
    ok
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
