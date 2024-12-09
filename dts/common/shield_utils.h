/* auto-generated by shields.cmake, don't edit */
#ifndef __DT_SHIELD_UTIL_H
#define __DT_SHIELD_UTIL_H

#include <zephyr/dt-bindings/dt-util.h>

#define SHIELD_OPTION_DEFINED(opt)                                                                 \
	COND_CODE_1(UTIL_CAT(UTIL_CAT(SHIELD_OPTION_, UTIL_CAT(opt, _DEFINED))),                   \
                           (1), (0))

#define SHIELD_OPTION(opt)                                                                         \
	COND_CODE_1(UTIL_CAT(SHIELD_OPTION_, UTIL_CAT(opt, _DEFINED)),                             \
                           (UTIL_CAT(SHIELD_OPTION_, opt)),                                        \
                           (UTIL_CAT(SHIELD_OPTION_DEFAULT_, opt)))

#define SHIELD_PREFIX_0X(x)           UTIL_CAT(0x, x)
#define SHIELD_CONNECTOR_NAME_DEFINED SHIELD_OPTION_DEFINED(CONNECTOR_NAME)
#define SHIELD_CONNECTOR_NAME         SHIELD_OPTION(CONNECTOR_NAME)

#endif /* __DT_SHIELD_UTIL_H */
