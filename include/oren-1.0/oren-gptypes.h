/* Oren - a streaming media software development kit.
 *
 * Copyright © 2012 TinySoft, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __OREN_GP_TYPES_H__
#define __OREN_GP_TYPES_H__

#include "oren-dcenums.h"
#include <gimo.h>

G_BEGIN_DECLS

/**
 * OrenGPUserPrivilege:
 * @OREN_GPUSER_PRIVILEGE_ROOT: Root Privilege
 * @OREN_GPUSER_PRIVILEGE_RESERVED1: Reserved Privilege
 * @OREN_GPUSER_PRIVILEGE_RESERVED2: Reserved Privilege
 * @OREN_GPUSER_PRIVILEGE_MANAGER: System Manager
 * @OREN_GPUSER_PRIVILEGE_RESERVED3: Reserved Privilege
 * @OREN_GPUSER_PRIVILEGE_RESERVED4: Reserved Privilege
 * @OREN_GPUSER_PRIVILEGE_USER: Registered User
 * @OREN_GPUSER_PRIVILEGE_RESERVED5: Reserved Privilege
 * @OREN_GPUSER_PRIVILEGE_RESERVED6: Reserved Privilege
 * @OREN_GPUSER_PRIVILEGE_GUEST: Guest
 */
typedef enum {
    OREN_GPUSER_PRIVILEGE_ROOT,
    OREN_GPUSER_PRIVILEGE_RESERVED1,
    OREN_GPUSER_PRIVILEGE_RESERVED2,
    OREN_GPUSER_PRIVILEGE_MANAGER,
    OREN_GPUSER_PRIVILEGE_RESERVED3,
    OREN_GPUSER_PRIVILEGE_RESERVED4,
    OREN_GPUSER_PRIVILEGE_USER,
    OREN_GPUSER_PRIVILEGE_RESERVED5,
    OREN_GPUSER_PRIVILEGE_RESERVED6,
    OREN_GPUSER_PRIVILEGE_GUEST
} OrenGPUserPrivilege;

typedef struct _OrenGPClient OrenGPClient;
typedef struct _OrenGPChannel OrenGPChannel;
typedef struct _OrenGPUser OrenGPUser;
typedef struct _OrenGPHandler OrenGPHandler;
typedef struct _OrenGPDispatch OrenGPDispatch;
typedef struct _OrenGPFactory OrenGPFactory;

GType oren_gpuser_privilege_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_GPUSER_PRIVILEGE (oren_gpuser_privilege_get_type ())

G_END_DECLS

#endif /* __OREN_GP_TYPES_H__ */
