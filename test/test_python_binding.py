"""
Python binding test for yupiter.

Run with: python test/test_python_binding.py
"""
import sys
sys.path.insert(0, "/usr/lib/python3.14/site-packages")

import yupiter


def test_import():
    assert yupiter is not None
    assert hasattr(yupiter, "yupiter_printf")
    assert hasattr(yupiter, "yupiter_malloc")


def test_stream_constants():
    assert yupiter.STDOUT > 0
    assert yupiter.STDERR > 0
    assert yupiter.STDIN > 0


def test_malloc_free():
    ptr = yupiter.yupiter_malloc(64)
    assert isinstance(ptr, int)
    assert ptr > 0
    yupiter.yupiter_free(ptr)


def test_printf():
    result = yupiter.yupiter_printf("hello test\n")
    assert result is not None
    assert result > 0


def test_fprintf_stdout():
    result = yupiter.yupiter_fprintf(yupiter.STDOUT, "fprintf stdout\n")
    assert result is not None
    assert result > 0


def test_realloc():
    ptr = yupiter.yupiter_malloc(32)
    assert ptr > 0
    new_ptr = yupiter.yupiter_realloc(ptr, 64)
    assert new_ptr is not None
    assert new_ptr > 0
    yupiter.yupiter_free(new_ptr)


def test_malloc_safe():
    ptr = yupiter.yupiter_malloc_safe(2, 4)
    assert ptr > 0
    yupiter.yupiter_free_safe(ptr)


def test_realloc_safe():
    ptr = yupiter.yupiter_malloc_safe(1, 8)
    assert ptr > 0
    new_ptr = yupiter.yupiter_realloc_safe(ptr, 2, 8)
    assert new_ptr > 0
    yupiter.yupiter_free_safe(new_ptr)


def test_zero_allocation():
    ptr = yupiter.yupiter_malloc(0)
    assert ptr is None


if __name__ == "__main__":
    test_import()
    test_stream_constants()
    test_malloc_free()
    test_printf()
    test_fprintf_stdout()
    test_realloc()
    test_malloc_safe()
    test_realloc_safe()
    test_zero_allocation()
    print("All tests passed.")
