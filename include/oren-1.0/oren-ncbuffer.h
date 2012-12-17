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
#ifndef __OREN_NC_BUFFER_H__
#define __OREN_NC_BUFFER_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCBUFFER (oren_ncbuffer_get_type())
#define OREN_NCBUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCBUFFER, OrenNCBuffer))
#define OREN_IS_NCBUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCBUFFER))
#define OREN_NCBUFFER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCBUFFER, OrenNCBufferClass))
#define OREN_IS_NCBUFFER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCBUFFER))
#define OREN_NCBUFFER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCBUFFER, OrenNCBufferClass))

typedef struct _OrenNCBufferPrivate OrenNCBufferPrivate;
typedef struct _OrenNCBufferClass OrenNCBufferClass;

struct _OrenNCBuffer {
    GObject parent_instance;
    OrenNCBufferPrivate *priv;
};

struct _OrenNCBufferClass {
    GObjectClass parent_class;
};

GType oren_ncbuffer_get_type (void) G_GNUC_CONST;

OrenNCBuffer* oren_ncbuffer_new (void);

void oren_ncbuffer_reset (OrenNCBuffer *self);

gboolean oren_ncbuffer_equal (OrenNCBuffer *self,
                              OrenNCBuffer *other);

void oren_ncbuffer_copy_data (OrenNCBuffer *self,
                              OrenNCBuffer *from);

OrenNCBuffer* oren_ncbuffer_clone (OrenNCBuffer *self);

void oren_ncbuffer_lock (OrenNCBuffer *self,
                         OrenNCBufferLock mask);

void oren_ncbuffer_unlock (OrenNCBuffer *self,
                           OrenNCBufferLock mask);

gboolean oren_ncbuffer_is_locked (OrenNCBuffer *self,
                                  OrenNCBufferLock mask);

gsize oren_ncbuffer_get_read (OrenNCBuffer *self);

void oren_ncbuffer_set_read (OrenNCBuffer *self, gsize pos);

gsize oren_ncbuffer_get_write (OrenNCBuffer *self);

void oren_ncbuffer_set_write (OrenNCBuffer *self, gsize pos);

gpointer oren_ncbuffer_data_ptr (OrenNCBuffer *self);

gsize oren_ncbuffer_data_length (OrenNCBuffer *self);

gconstpointer oren_ncbuffer_read_ptr (OrenNCBuffer *self);

gsize oren_ncbuffer_unread_length (OrenNCBuffer *self);

void oren_ncbuffer_read_adv (OrenNCBuffer *self, gsize len);

gpointer oren_ncbuffer_write_ptr (OrenNCBuffer *self,
                                  gsize require_space);

void oren_ncbuffer_write_adv (OrenNCBuffer *self, gsize len);

gpointer oren_ncbuffer_insert_ptr (OrenNCBuffer *self,
                                   gsize pos,
                                   gsize require_space);

guint8 oren_ncbuffer_read_u8 (OrenNCBuffer *self);

guint16 oren_ncbuffer_read_u16 (OrenNCBuffer *self);

guint32 oren_ncbuffer_read_u32 (OrenNCBuffer *self);

guint64 oren_ncbuffer_read_u64 (OrenNCBuffer *self);

void oren_ncbuffer_write_u8 (OrenNCBuffer *self, guint8 val);

void oren_ncbuffer_write_u16 (OrenNCBuffer *self, guint16 val);

void oren_ncbuffer_write_u32 (OrenNCBuffer *self, guint32 val);

void oren_ncbuffer_write_u64 (OrenNCBuffer *self, guint64 val);

G_END_DECLS

#endif /* __OREN_NC_BUFFER_H__ */
