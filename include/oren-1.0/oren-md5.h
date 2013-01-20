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
#ifndef __OREN_MD5_H__
#define __OREN_MD5_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_MD5 (oren_md5_get_type())
#define OREN_MD5(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_MD5, OrenMd5))
#define OREN_IS_MD5(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_MD5))
#define OREN_MD5_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_MD5, OrenMd5Class))
#define OREN_IS_MD5_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_MD5))
#define OREN_MD5_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_MD5, OrenMd5Class))

typedef struct _OrenMd5Private OrenMd5Private;
typedef struct _OrenMd5Class OrenMd5Class;

struct _OrenMd5 {
    GObject parent_instance;
    OrenMd5Private *priv;
};

struct _OrenMd5Class {
    GObjectClass parent_class;
};

GType oren_md5_get_type (void) G_GNUC_CONST;

OrenMd5* oren_md5_new (void);

void oren_md5_reset (OrenMd5 *self);

void oren_md5_append_string (OrenMd5 *self,
                             const gchar *string);

void oren_md5_append_buffer (OrenMd5 *self,
                             OrenNCBuffer *buffer);

const gchar* oren_md5_get_digest (OrenMd5 *self);

gchar* oren_md5_dup_digest (OrenMd5 *self);

G_END_DECLS

#endif /* __OREN_MD5_H__ */
