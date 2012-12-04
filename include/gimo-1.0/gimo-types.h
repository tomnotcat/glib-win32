/* GIMO - A plugin framework based on GObject.
 *
 * Copyright (C) 2012 TinySoft, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __GIMO_TYPES_H__
#define __GIMO_TYPES_H__

#include "gimo-enums.h"

G_BEGIN_DECLS

typedef struct _GimoContext GimoContext;
typedef struct _GimoPlugin GimoPlugin;
typedef struct _GimoRequire GimoRequire;
typedef struct _GimoExtPoint GimoExtPoint;
typedef struct _GimoExtension GimoExtension;
typedef struct _GimoExtConfig GimoExtConfig;
typedef struct _GimoModule GimoModule;
typedef struct _GimoLoader GimoLoader;
typedef struct _GimoFactory GimoFactory;
typedef struct _GimoLoadable GimoLoadable;
typedef struct _GimoArchive GimoArchive;
typedef struct _GPtrArray GimoObjectArray;
typedef struct _GimoRunnable GimoRunnable;
typedef struct _GimoSignalBus GimoSignalBus;

GType gimo_object_array_get_type (void) G_GNUC_CONST;
#define GIMO_TYPE_OBJECT_ARRAY (gimo_object_array_get_type ())

#define GIMO_REGISTER_TYPE(type) \
    G_STMT_START { \
        GType _type = type; \
        (void) _type; \
    } G_STMT_END

G_END_DECLS

#endif /* __GIMO_TYPES_H__ */
