use pyo3::prelude::*;
use std::ffi::CString;
use std::os::raw::{c_char, c_void};

// FFI declarations - use link_name to reference C symbols while avoiding naming conflicts
extern "C" {
    #[link_name = "yupiter_printf"]
    fn ffi_printf(format: *const c_char) -> i32;

    #[link_name = "yupiter_fprintf"]
    fn ffi_fprintf(stream: *mut c_void, format: *const c_char) -> i32;

    #[link_name = "yupiter_malloc"]
    fn ffi_malloc(size: usize) -> *mut c_void;

    #[link_name = "yupiter_free"]
    fn ffi_free(ptr: *mut c_void);

    #[link_name = "yupiter_realloc"]
    fn ffi_realloc(ptr: *mut c_void, size: usize) -> *mut c_void;

    #[link_name = "yupiter_malloc_safe"]
    fn ffi_malloc_safe(count: usize, size: usize) -> *mut c_void;

    #[link_name = "yupiter_free_safe"]
    fn ffi_free_safe(ptr: *mut c_void);

    #[link_name = "yupiter_realloc_safe"]
    fn ffi_realloc_safe(ptr: *mut c_void, count: usize, size: usize) -> *mut c_void;

    #[link_name = "yupiter_stdout"]
    static ffi_stdout: *mut c_void;

    #[link_name = "yupiter_stderr"]
    static ffi_stderr: *mut c_void;

    #[link_name = "yupiter_stdin"]
    static ffi_stdin: *mut c_void;
}

// --- Output functions ---

#[pyfunction]
fn yupiter_printf(format: &str) -> Option<i32> {
    let c_str = CString::new(format).ok()?;
    let result = unsafe { ffi_printf(c_str.as_ptr()) };
    if result < 0 {
        None
    } else {
        Some(result)
    }
}

#[pyfunction]
fn yupiter_fprintf(stream: usize, format: &str) -> Option<i32> {
    let c_str = CString::new(format).ok()?;
    let result = unsafe { ffi_fprintf(stream as *mut c_void, c_str.as_ptr()) };
    if result < 0 {
        None
    } else {
        Some(result)
    }
}

// --- Basic memory functions ---

#[pyfunction]
fn yupiter_malloc(size: usize) -> Option<usize> {
    let ptr = unsafe { ffi_malloc(size) };
    if ptr.is_null() {
        None
    } else {
        Some(ptr as usize)
    }
}

#[pyfunction]
fn yupiter_free(ptr: usize) {
    unsafe { ffi_free(ptr as *mut c_void) };
}

#[pyfunction]
fn yupiter_realloc(ptr: usize, size: usize) -> Option<usize> {
    let result = unsafe { ffi_realloc(ptr as *mut c_void, size) };
    if result.is_null() {
        None
    } else {
        Some(result as usize)
    }
}

// --- Safe memory functions ---

#[pyfunction]
fn yupiter_malloc_safe(count: usize, size: usize) -> usize {
    if count != 0 && size > usize::MAX / count {
        panic!("size overflow");
    }
    let total = count * size;
    let ptr = unsafe { ffi_malloc_safe(count, size) };
    if ptr.is_null() {
        if total == 0 {
            return 0;
        }
        panic!("allocation failed");
    }
    ptr as usize
}

#[pyfunction]
fn yupiter_free_safe(ptr: usize) {
    if ptr != 0 {
        unsafe { ffi_free_safe(ptr as *mut c_void) };
    }
}

#[pyfunction]
fn yupiter_realloc_safe(ptr: usize, count: usize, size: usize) -> usize {
    if count != 0 && size > usize::MAX / count {
        panic!("size overflow");
    }
    let result = unsafe { ffi_realloc_safe(ptr as *mut c_void, count, size) };
    if result.is_null() {
        if count == 0 && size == 0 {
            return 0;
        }
        panic!("reallocation failed");
    }
    result as usize
}

// --- Module definition ---

#[pymodule]
fn yupiter(m: &Bound<PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(yupiter_printf, m)?)?;
    m.add_function(wrap_pyfunction!(yupiter_fprintf, m)?)?;
    m.add_function(wrap_pyfunction!(yupiter_malloc, m)?)?;
    m.add_function(wrap_pyfunction!(yupiter_free, m)?)?;
    m.add_function(wrap_pyfunction!(yupiter_realloc, m)?)?;
    m.add_function(wrap_pyfunction!(yupiter_malloc_safe, m)?)?;
    m.add_function(wrap_pyfunction!(yupiter_free_safe, m)?)?;
    m.add_function(wrap_pyfunction!(yupiter_realloc_safe, m)?)?;

    // Stream constants
    m.add("STDOUT", unsafe { ffi_stdout as usize })?;
    m.add("STDERR", unsafe { ffi_stderr as usize })?;
    m.add("STDIN", unsafe { ffi_stdin as usize })?;

    Ok(())
}