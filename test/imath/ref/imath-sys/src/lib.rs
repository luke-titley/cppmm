#[repr(transparent)] 
pub struct Exception(u32);

impl Exception {
    pub fn into_result(self) -> Result<(), Error> {
        match self.0 {
            0 => {
                Ok(())
            }

            std::u32::MAX => {
                panic!("Unhandled exception")
            }
            _ => {
                panic!("Unexpected exception value")
            }
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum Error {
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        None
    }
}

use std::fmt;
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {

        Ok(())
    }
}
pub mod imath_vec;
pub use imath_vec::Imath_2_5__Vec3_float__t as Imath_V3f_t;
pub use imath_vec::Imath_2_5__Vec3_int__t as Imath_V3i_t;

pub use imath_vec::Imath_2_5__Vec3_float__Vec3 as Imath_V3f_Vec3;
pub use imath_vec::Imath_2_5__Vec3_float__Vec3_1 as Imath_V3f_Vec3_1;
pub use imath_vec::Imath_2_5__Vec3_float__setValue as Imath_V3f_setValue;
pub use imath_vec::Imath_2_5__Vec3_float__dot as Imath_V3f_dot;
pub use imath_vec::Imath_2_5__Vec3_float__cross as Imath_V3f_cross;
pub use imath_vec::Imath_2_5__Vec3_float__op_iadd as Imath_V3f_op_iadd;
pub use imath_vec::Imath_2_5__Vec3_float__length as Imath_V3f_length;
pub use imath_vec::Imath_2_5__Vec3_float__length2 as Imath_V3f_length2;
pub use imath_vec::Imath_2_5__Vec3_float__normalize as Imath_V3f_normalize;
pub use imath_vec::Imath_2_5__Vec3_float__normalized as Imath_V3f_normalized;
pub use imath_vec::Imath_2_5__Vec3_int__Vec3 as Imath_V3i_Vec3;
pub use imath_vec::Imath_2_5__Vec3_int__Vec3_1 as Imath_V3i_Vec3_1;
pub use imath_vec::Imath_2_5__Vec3_int__dot as Imath_V3i_dot;
pub use imath_vec::Imath_2_5__Vec3_int__cross as Imath_V3i_cross;
pub use imath_vec::Imath_2_5__Vec3_int__op_iadd as Imath_V3i_op_iadd;
pub use imath_vec::Imath_2_5__Vec3_int__length as Imath_V3i_length;
pub use imath_vec::Imath_2_5__Vec3_int__length2 as Imath_V3i_length2;
pub use imath_vec::Imath_2_5__Vec3_int__normalize as Imath_V3i_normalize;
pub use imath_vec::Imath_2_5__Vec3_int__normalized as Imath_V3i_normalized;
pub mod imath_box;
pub use imath_box::Imath_2_5__Box_Imath__Vec3_float___t as Imath_Box3f_t;
pub use imath_box::Imath_2_5__Box_Imath__Vec3_int___t as Imath_Box3i_t;

pub use imath_box::Imath_2_5__Box_Imath__Vec3_float___extendBy as Imath_Box3f_extendBy;
pub use imath_box::Imath_2_5__Box_Imath__Vec3_float___extendBy_1 as Imath_Box3f_extendBy_1;
pub use imath_box::Imath_2_5__Box_Imath__Vec3_int___extendBy as Imath_Box3i_extendBy;
pub use imath_box::Imath_2_5__Box_Imath__Vec3_int___extendBy_1 as Imath_Box3i_extendBy_1;


#[cfg(test)]
mod test;
