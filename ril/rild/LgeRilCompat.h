#pragma once

#include <telephony/ril.h>

#ifdef __cplusplus
extern "C" {
#endif
const RIL_RadioFunctions *LGE_RIL_Init(
        const RIL_RadioFunctions *(*init)(const struct RIL_Env *, int, char **),
        const struct RIL_Env *env, int argc, char **argv);
#ifdef __cplusplus
}
#endif
