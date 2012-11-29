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
#ifndef __OREN_NC_UNPACKER_H__
#define __OREN_NC_UNPACKER_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCUNPACKER (oren_ncunpacker_get_type())
#define OREN_NCUNPACKER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCUNPACKER, OrenNCUnpacker))
#define OREN_IS_NCUNPACKER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCUNPACKER))
#define OREN_NCUNPACKER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCUNPACKER, OrenNCUnpackerClass))
#define OREN_IS_NCUNPACKER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCUNPACKER))
#define OREN_NCUNPACKER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCUNPACKER, OrenNCUnpackerClass))

typedef struct _OrenNCUnpackerPrivate OrenNCUnpackerPrivate;
typedef struct _OrenNCUnpackerClass OrenNCUnpackerClass;

struct _OrenNCUnpacker
{
    GObject parent_instance;
    OrenNCUnpackerPrivate *priv;
};

struct _OrenNCUnpackerClass
{
    GObjectClass parent_class;
};

GType oren_ncunpacker_get_type(void) G_GNUC_CONST;

OrenNCUnpacker* oren_ncunpacker_new(void);

void oren_ncunpacker_reset(OrenNCUnpacker *self);

gboolean oren_ncunpacker_unpack_block(OrenNCUnpacker *self,
                                      gconstpointer buffer,
                                      gsize size);

gboolean oren_ncunpacker_unpack_from(OrenNCUnpacker *self,
                                     OrenNCBuffer *buffer);

gsize oren_ncunpacker_data_count(OrenNCUnpacker *self);

gconstpointer oren_ncunpacker_get_data(OrenNCUnpacker *self,
                                       guint index,
                                       gsize *size);

gsize oren_ncunpacker_extract_to(OrenNCUnpacker *self,
                                 guint index,
                                 OrenNCBuffer *buffer);

void oren_ncunpacker_pop_data(OrenNCUnpacker *self);

G_END_DECLS

#endif /* __OREN_NC_UNPACKER_H__ */
