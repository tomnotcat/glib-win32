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
#ifndef __GIMO_EXTCONFIG_H__
#define __GIMO_EXTCONFIG_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_EXTCONFIG (gimo_ext_config_get_type())
#define GIMO_EXTCONFIG(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_EXTCONFIG, GimoExtConfig))
#define GIMO_IS_EXTCONFIG(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_EXTCONFIG))
#define GIMO_EXTCONFIG_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_EXTCONFIG, GimoExtConfigClass))
#define GIMO_IS_EXTCONFIG_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_EXTCONFIG))
#define GIMO_EXTCONFIG_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_EXTCONFIG, GimoExtConfigClass))

typedef struct _GimoExtConfigPrivate GimoExtConfigPrivate;
typedef struct _GimoExtConfigClass GimoExtConfigClass;

struct _GimoExtConfig {
    GObject parent_instance;
    GimoExtConfigPrivate *priv;
};

struct _GimoExtConfigClass {
    GObjectClass parent_class;
};

GType gimo_ext_config_get_type (void) G_GNUC_CONST;

GimoExtConfig* gimo_ext_config_new (const gchar *name,
                                    const gchar *value,
                                    GPtrArray *configs);

const gchar* gimo_ext_config_get_name (GimoExtConfig *self);

const gchar* gimo_ext_config_get_value (GimoExtConfig *self);

GimoExtConfig* gimo_ext_config_get_config (GimoExtConfig *self,
                                           const gchar *name_space);

GPtrArray* gimo_ext_config_get_configs (GimoExtConfig *self);

G_END_DECLS

#endif /* __GIMO_EXTCONFIG_H__ */
