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
#ifndef __GIMO_EXTPOINT_H__
#define __GIMO_EXTPOINT_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_EXTPOINT (gimo_ext_point_get_type())
#define GIMO_EXTPOINT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_EXTPOINT, GimoExtPoint))
#define GIMO_IS_EXTPOINT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_EXTPOINT))
#define GIMO_EXTPOINT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_EXTPOINT, GimoExtPointClass))
#define GIMO_IS_EXTPOINT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_EXTPOINT))
#define GIMO_EXTPOINT_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_EXTPOINT, GimoExtPointClass))

typedef struct _GimoExtPointPrivate GimoExtPointPrivate;
typedef struct _GimoExtPointClass GimoExtPointClass;

struct _GimoExtPoint {
    GObject parent_instance;
    GimoExtPointPrivate *priv;
};

struct _GimoExtPointClass {
    GObjectClass parent_class;
};

GType gimo_ext_point_get_type (void) G_GNUC_CONST;

GimoExtPoint* gimo_ext_point_new (const gchar *id,
                                  const gchar *name);

const gchar* gimo_ext_point_get_local_id (GimoExtPoint *self);

const gchar* gimo_ext_point_get_id (GimoExtPoint *self);

const gchar* gimo_ext_point_get_name (GimoExtPoint *self);

GimoPlugin* gimo_ext_point_query_plugin (GimoExtPoint *self);

G_END_DECLS

#endif /* __GIMO_EXTPOINT_H__ */
