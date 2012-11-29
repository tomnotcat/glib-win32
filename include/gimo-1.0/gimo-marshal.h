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
#ifndef __GIMO_MARSHAL_H__
#define __GIMO_MARSHAL_H__

#include "gimo-types.h"

G_BEGIN_DECLS

void _gimo_marshal_VOID__OBJECT_ENUM_ENUM (GClosure *closure,
                                           GValue *return_value G_GNUC_UNUSED,
                                           guint n_param_values,
                                           const GValue *param_values,
                                           gpointer invocation_hint G_GNUC_UNUSED,
                                           gpointer marshal_data);

void _gimo_marshal_BOOLEAN__VOID (GClosure *closure,
                                  GValue *return_value G_GNUC_UNUSED,
                                  guint n_param_values,
                                  const GValue *param_values,
                                  gpointer invocation_hint G_GNUC_UNUSED,
                                  gpointer marshal_data);

void _gimo_marshal_OBJECT__VOID (GClosure *closure,
                                 GValue *return_value G_GNUC_UNUSED,
                                 guint n_param_values,
                                 const GValue *param_values,
                                 gpointer invocation_hint G_GNUC_UNUSED,
                                 gpointer marshal_data);

G_END_DECLS

#endif /* __GIMO_MARSHAL_H__ */
