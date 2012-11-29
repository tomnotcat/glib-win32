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
#ifndef __GIMO_DLMODULE_H__
#define __GIMO_DLMODULE_H__

#include "gimo-loadable.h"
#include "gimo-module.h"
#include <gmodule.h>

G_BEGIN_DECLS

#define GIMO_TYPE_DLMODULE (gimo_dlmodule_get_type())
#define GIMO_DLMODULE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_DLMODULE, GimoDlmodule))
#define GIMO_IS_DLMODULE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_DLMODULE))
#define GIMO_DLMODULE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_DLMODULE, GimoDlmoduleClass))
#define GIMO_IS_DLMODULE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_DLMODULE))
#define GIMO_DLMODULE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_DLMODULE, GimoDlmoduleClass))

typedef struct _GimoDlmodule GimoDlmodule;
typedef struct _GimoDlmodulePrivate GimoDlmodulePrivate;
typedef struct _GimoDlmoduleClass GimoDlmoduleClass;

struct _GimoDlmodule {
    GObject parent_instance;
    GimoDlmodulePrivate *priv;
};

struct _GimoDlmoduleClass {
    GObjectClass parent_class;
};

GType gimo_dlmodule_get_type (void) G_GNUC_CONST;

GimoDlmodule* gimo_dlmodule_new (void);

GModule* _gimo_dlmodule_get_gmodule (GimoDlmodule *self);

G_END_DECLS

#endif /* __GIMO_DLMODULE_H__ */
