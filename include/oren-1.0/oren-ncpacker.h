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
#ifndef __OREN_NC_PACKER_H__
#define __OREN_NC_PACKER_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCPACKER (oren_ncpacker_get_type())
#define OREN_NCPACKER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCPACKER, OrenNCPacker))
#define OREN_IS_NCPACKER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCPACKER))
#define OREN_NCPACKER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCPACKER, OrenNCPackerClass))
#define OREN_IS_NCPACKER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCPACKER))
#define OREN_NCPACKER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCPACKER, OrenNCPackerClass))

#define OREN_NCPACKER_BLOCK_HEAD_SIZE    (sizeof (guint32))
#define OREN_NCPACKER_DATA_HEAD_SIZE     (sizeof (guint32))

typedef struct _OrenNCPackerPrivate OrenNCPackerPrivate;
typedef struct _OrenNCPackerClass OrenNCPackerClass;

struct _OrenNCPacker
{
    GObject parent_instance;
    OrenNCPackerPrivate *priv;
};

struct _OrenNCPackerClass
{
    GObjectClass parent_class;
};

GType oren_ncpacker_get_type(void) G_GNUC_CONST;

OrenNCPacker* oren_ncpacker_new(gsize block_size);

void oren_ncpacker_reset(OrenNCPacker *self);

gboolean oren_ncpacker_is_buffer_empty(OrenNCPacker *self);

gboolean oren_ncpacker_flush_buffer(OrenNCPacker *self);

gboolean oren_ncpacker_pack_data(OrenNCPacker *self,
                                 gconstpointer buffer,
                                 gsize size);

gboolean oren_ncpacker_pack_from(OrenNCPacker *self,
                                 OrenNCBuffer *buffer);

gsize oren_ncpacker_block_size(OrenNCPacker *self);

gsize oren_ncpacker_block_count(OrenNCPacker *self);

gconstpointer oren_ncpacker_get_block(OrenNCPacker *self,
                                      guint index,
                                      gsize *size);

gsize oren_ncpacker_extract_to(OrenNCPacker *self,
                               guint index,
                               OrenNCBuffer *buffer);

void oren_ncpacker_pop_block(OrenNCPacker *self);

G_END_DECLS

#endif /* __OREN_NC_PACKER_H__ */
