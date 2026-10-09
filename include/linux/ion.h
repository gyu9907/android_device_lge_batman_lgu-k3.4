/*
 * Let each module select its own ION ABI. The msm8660 HALs explicitly include
 * KERNEL_OBJ/usr/include; platform libion supplies its own kernel-headers.
 * A device-global copy of the legacy structs shadows Pie libion's declarations
 * and must not replace the header chosen by those module include paths.
 */
#ifndef BATMAN_LGU_ION_FORWARD_H
#define BATMAN_LGU_ION_FORWARD_H
#include_next <linux/ion.h>
#endif
