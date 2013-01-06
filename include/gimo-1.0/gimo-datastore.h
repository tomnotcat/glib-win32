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
#ifndef __GIMO_DATASTORE_H__
#define __GIMO_DATASTORE_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_DATASTORE (gimo_data_store_get_type())
#define GIMO_DATASTORE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_DATASTORE, GimoDataStore))
#define GIMO_IS_DATASTORE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_DATASTORE))
#define GIMO_DATASTORE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_DATASTORE, GimoDataStoreClass))
#define GIMO_IS_DATASTORE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_DATASTORE))
#define GIMO_DATASTORE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_DATASTORE, GimoDataStoreClass))

typedef struct _GimoDataStorePrivate GimoDataStorePrivate;
typedef struct _GimoDataStoreClass GimoDataStoreClass;

struct _GimoDataStore {
    GObject parent_instance;
    GimoDataStorePrivate *priv;
};

struct _GimoDataStoreClass {
    GObjectClass parent_class;
};

GType gimo_data_store_get_type (void) G_GNUC_CONST;

GimoDataStore* gimo_data_store_new (void);

void gimo_data_store_set (GimoDataStore *self,
                          const gchar *key,
                          const GValue *value);

const GValue* gimo_data_store_get (GimoDataStore *self,
                                   const gchar *key);

void gimo_data_store_foreach (GimoDataStore *self,
                              GTraverseFunc func,
                              gpointer user_data);

void gimo_data_store_set_string (GimoDataStore *self,
                                 const gchar *key,
                                 const gchar *value);

const gchar* gimo_data_store_get_string (GimoDataStore *self,
                                         const gchar *key);

void gimo_data_store_set_object (GimoDataStore *self,
                                 const gchar *key,
                                 GObject *value);

GObject* gimo_data_store_get_object (GimoDataStore *self,
                                     const gchar *key);

void gimo_bind (GObject *object,
                const gchar *key,
                const GValue *value);

const GValue* gimo_lookup (GObject *object,
                           const gchar *key);

void gimo_bind_string (GObject *object,
                       const gchar *key,
                       const gchar *value);

const gchar* gimo_lookup_string (GObject *object,
                                 const gchar *key);

void gimo_bind_object (GObject *object,
                       const gchar *key,
                       GObject *value);

GObject* gimo_lookup_object (GObject *object,
                             const gchar *key);

G_END_DECLS

#endif /* __GIMO_DATASTORE_H__ */
