// Hello from Rust on AmigaOS: a 68k ELF program (LLVM's m68k backend,
// 68040, soft float) that elf.library loads. core and alloc only, the
// allocator on one exec pool (crt/amiga_shim.c), output through
// dos.library's PutStr.
// Copyright (c) 2026 AmiCradle Team. MIT.
#![no_std]

extern crate alloc;

use alloc::string::String;
use alloc::vec::Vec;
use core::alloc::{GlobalAlloc, Layout};

extern "C" {
    fn amiga_open() -> i32;
    fn amiga_close();
    fn amiga_putstr(s: *const u8) -> i32;
    fn amiga_putnum(n: u32) -> i32;
    fn amiga_alloc(size: usize) -> *mut u8;
    fn amiga_free(mem: *mut u8, size: usize);
}

/// Exec pools give 8-byte aligned blocks; a larger alignment takes a
/// bigger block and a word in front of the aligned one saying where it began.
struct ExecPool;

unsafe impl GlobalAlloc for ExecPool {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        if l.align() <= 8 {
            return amiga_alloc(l.size().max(1));
        }
        let raw = amiga_alloc(l.size() + l.align() + 4);
        if raw.is_null() {
            return raw;
        }
        let at = (raw as usize + 4 + l.align() - 1) & !(l.align() - 1);
        *((at - 4) as *mut usize) = raw as usize;
        at as *mut u8
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        if l.align() <= 8 {
            amiga_free(p, l.size().max(1));
        } else {
            amiga_free(*((p as usize - 4) as *const usize) as *mut u8, l.size() + l.align() + 4);
        }
    }
}

#[global_allocator]
static POOL: ExecPool = ExecPool;

fn put(s: &str) {
    let mut c: Vec<u8> = Vec::with_capacity(s.len() + 1);
    c.extend_from_slice(s.as_bytes());
    c.push(0);
    unsafe { amiga_putstr(c.as_ptr()); }
}

#[no_mangle]
pub extern "C" fn amiga_main(args: *const u8, len: i32) -> i32 {
    if unsafe { amiga_open() } == 0 {
        return 20;
    }
    hello(args, len);           // everything it allocated is dropped by now
    unsafe { amiga_close(); }
    0
}

fn hello(args: *const u8, len: i32) {
    let words: Vec<String> = ["Hello", "from", "Rust", "on", "AmigaOS"].iter().map(|w| String::from(*w)).collect();
    put(&words.join(" "));
    put("\n");
    let squares: Vec<u32> = (1..=10).map(|n| n * n).collect();
    // numbers go out through the C side: LLVM's m68k backend (rustc 1.97)
    // miscompiles core's integer formatting (README.md, "Rust")
    put("squares:");
    for n in &squares {
        put(" ");
        unsafe { amiga_putnum(*n); }
    }
    put("\n");
    let line = unsafe { core::slice::from_raw_parts(args, len.max(0) as usize) };
    put("argument bytes: ");
    unsafe { amiga_putnum(line.len() as u32); }
    put("\n");
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    unsafe {
        amiga_putstr(b"Rust panic\n\0".as_ptr());
    }
    loop {}
}
