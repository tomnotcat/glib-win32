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
#ifndef __GIMO_UTILS_H__
#define __GIMO_UTILS_H__

#include "gimo-types.h"

G_BEGIN_DECLS

gchar* _gimo_parse_extension_id (const gchar *ext_id,
                                 gchar **local_id);

gint _gimo_gtree_string_compare (gconstpointer a,
                                 gconstpointer b,
                                 gpointer user_data);

GPtrArray* _gimo_clone_object_array (GPtrArray *arr,
                                     GType type,
                                     void (*func) (gpointer, gpointer),
                                     gpointer user_data);

gpointer gimo_safe_cast (gpointer object, GType type);

gchar* _gimo_symbol_from_type_name (const gchar *name);

GType gimo_resolve_type_lazily (const gchar *name);

G_END_DECLS

#endif /* __GIMO_UTILS_H__ */
