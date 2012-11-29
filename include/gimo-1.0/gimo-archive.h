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
#ifndef __GIMO_ARCHIVE_H__
#define __GIMO_ARCHIVE_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_ARCHIVE (gimo_archive_get_type())
#define GIMO_ARCHIVE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_ARCHIVE, GimoArchive))
#define GIMO_IS_ARCHIVE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_ARCHIVE))
#define GIMO_ARCHIVE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_ARCHIVE, GimoArchiveClass))
#define GIMO_IS_ARCHIVE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_ARCHIVE))
#define GIMO_ARCHIVE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_ARCHIVE, GimoArchiveClass))

typedef struct _GimoArchivePrivate GimoArchivePrivate;
typedef struct _GimoArchiveClass GimoArchiveClass;

struct _GimoArchive {
    GObject parent_instance;
    GimoArchivePrivate *priv;
};

struct _GimoArchiveClass {
    GObjectClass parent_class;
    gboolean (*read) (GimoArchive *self,
                      const gchar *file_name);
    gboolean (*save) (GimoArchive *self,
                      const gchar *file_name);
};

GType gimo_archive_get_type (void) G_GNUC_CONST;

GimoArchive* gimo_archive_new (void);

gboolean gimo_archive_read (GimoArchive *self,
                            const gchar *file_name);

gboolean gimo_archive_save (GimoArchive *self,
                            const gchar *file_name);

gboolean gimo_archive_add_object (GimoArchive *self,
                                  const gchar *id,
                                  GObject *object);

void gimo_archive_remove_object (GimoArchive *self,
                                 const gchar *id);

GObject* gimo_archive_query_object (GimoArchive *self,
                                    const gchar *id);

GPtrArray* gimo_archive_query_objects (GimoArchive *self);

G_END_DECLS

#endif /* __GIMO_ARCHIVE_H__ */
