#![allow(non_snake_case)]
#![allow(non_camel_case_types)]
#![allow(non_upper_case_globals)]
#![allow(unused_imports)]
use crate::*;
use std::os::raw::*;

#[repr(C)]
pub struct std____cxx11__basic_string_char__t {
    _unused: [u8; 0],
}
#[repr(C)]
pub struct std__vector_std__string__t {
    _unused: [u8; 0],
}


extern "C" {

pub fn std____cxx11__basic_string_char__assign(this_: *mut std___cxx11_string_t, return_: *mut *mut std___cxx11_string_t, s: *const c_char, count: c_ulong) -> Exception;

pub fn std____cxx11__basic_string_char__c_str(this_: *const std___cxx11_string_t, return_: *mut *const c_char) -> Exception;

pub fn std__vector_std__string__vector(this_: *mut *mut std_vector_string_t) -> Exception;

pub fn std__vector_std__string__dtor(this_: *mut std_vector_string_t) -> Exception;


} // extern "C"
