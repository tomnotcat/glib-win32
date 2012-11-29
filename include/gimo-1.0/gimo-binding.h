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
#ifndef __GIMO_BINDING_H__
#define __GIMO_BINDING_H__

#include "gimo-types.h"

G_BEGIN_DECLS

void gimo_bind_object (GObject *object,
                       const gchar *key,
                       GObject *data);

void gimo_bind_string (GObject *object,
                       const gchar *key,
                       const gchar *data);

void gimo_binding_lock (GObject *object);

void gimo_binding_unlock (GObject *object);

GObject* gimo_lookup_object (GObject *object,
                             const gchar *key);

const gchar* gimo_lookup_string (GObject *object,
                                 const gchar *key);

GObject* gimo_query_object (GObject *object,
                            const gchar *key);

gchar* gimo_query_string (GObject *object,
                          const gchar *key);

G_END_DECLS

#endif /* __GIMO_BINDING_H__ */
