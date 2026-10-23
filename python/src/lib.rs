use pyo3::exceptions::{PyMemoryError, PyOverflowError, PyValueError};
use pyo3::prelude::*;
use std::ffi::CString;
use std::os::raw::{c_char, c_void};

// FFI declarations - use link_name to reference C symbols while avoiding naming conflicts
extern "C" {
    #[link_name = "yupiter_printf"]
    fn ffi_printf(format: *const c_char, ...) -> i32;

    #[link_name = "yupiter_fprintf"]
    fn ffi_fprintf(stream: *mut c_void, format: *const c_char, ...) -> i32;

    #[link_name = "yupiter_malloc"]
    fn ffi_malloc(size: usize) -> *mut c_void;

    #[link_name = "yupiter_free"]
    fn ffi_free(ptr: *mut c_void);

    #[link_name = "yupiter_realloc"]
    fn ffi_realloc(ptr: *mut c_void, size: usize) -> *mut c_void;

    #[link_name = "yupiter_free_checked"]
    fn ffi_free_checked(ptr: *mut c_void) -> std::os::raw::c_int;

    #[link_name = "yupiter_realloc_checked"]
    fn ffi_realloc_checked(
        ptr: *mut c_void,
        size: usize,
        result: *mut *mut c_void,
    ) -> std::os::raw::c_int;

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
    let result = unsafe { ffi_printf(c"%s".as_ptr(), c_str.as_ptr()) };
    if result < 0 {
        None
    } else {
        Some(result)
    }
}

#[pyfunction]
fn yupiter_fprintf(stream: usize, format: &str) -> Option<i32> {
    let c_str = CString::new(format).ok()?;
    let result = unsafe { ffi_fprintf(stream as *mut c_void, c"%s".as_ptr(), c_str.as_ptr()) };
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

fn allocation_size(count: usize, size: usize) -> PyResult<usize> {
    count
        .checked_mul(size)
        .ok_or_else(|| PyOverflowError::new_err("allocation size overflows"))
}

fn memory_status(status: std::os::raw::c_int) -> PyResult<()> {
    match status {
        0 => Ok(()),
        2 => Err(PyMemoryError::new_err("out of memory")),
        3 => Err(PyOverflowError::new_err("allocation size overflows")),
        _ => Err(PyValueError::new_err("invalid or corrupted heap pointer")),
    }
}

#[pyfunction]
fn yupiter_malloc_safe(count: usize, size: usize) -> PyResult<usize> {
    let total = allocation_size(count, size)?;
    if total == 0 {
        return Ok(0);
    }
    let ptr = unsafe { ffi_malloc(total) };
    if ptr.is_null() {
        return Err(PyMemoryError::new_err("out of memory"));
    }
    // The allocation is live and has at least total writable bytes.
    unsafe { std::ptr::write_bytes(ptr.cast::<u8>(), 0, total) };
    Ok(ptr as usize)
}

#[pyfunction]
fn yupiter_free_safe(ptr: usize) -> PyResult<()> {
    memory_status(unsafe { ffi_free_checked(ptr as *mut c_void) })
}

#[pyfunction]
fn yupiter_realloc_safe(ptr: usize, count: usize, size: usize) -> PyResult<usize> {
    let total = allocation_size(count, size)?;
    if ptr == 0 {
        return yupiter_malloc_safe(count, size);
    }
    let mut result = std::ptr::null_mut();
    memory_status(unsafe { ffi_realloc_checked(ptr as *mut c_void, total, &mut result) })?;
    Ok(result as usize)
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