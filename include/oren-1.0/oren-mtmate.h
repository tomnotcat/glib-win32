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
#ifndef __OREN_MT_MATE_H__
#define __OREN_MT_MATE_H__

#include "oren-mttypes.h"
#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_MTMATE (oren_mtmate_get_type())
#define OREN_MTMATE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_MTMATE, OrenMTMate))
#define OREN_IS_MTMATE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_MTMATE))
#define OREN_MTMATE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_MTMATE, OrenMTMateClass))
#define OREN_IS_MTMATE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_MTMATE))
#define OREN_MTMATE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_MTMATE, OrenMTMateClass))

typedef struct _OrenMTMatePrivate OrenMTMatePrivate;
typedef struct _OrenMTMateClass OrenMTMateClass;

struct timeval;

struct _OrenMTMate {
    GObject parent_instance;
    OrenMTMatePrivate *priv;
};

struct _OrenMTMateClass {
    GObjectClass parent_class;
};

GType oren_mtmate_get_type (void) G_GNUC_CONST;

OrenMTMate* oren_mtmate_new (OrenMTBranch *branch,
                             const gchar *mate_name,
                             guint32 mate_id,
                             OrenNCSockaddr *mate_addr,
                             gboolean is_parent);

const gchar* oren_mtmate_get_name (OrenMTMate *self);

guint32 oren_mtmate_get_id (OrenMTMate *self);

OrenNCSockaddr* oren_mtmate_get_address (OrenMTMate *self);

gboolean oren_mtmate_is_connected (OrenMTMate *self);

gboolean _oren_mtmate_work (OrenMTMate *self,
                            struct timeval *tv);

void _oren_mtmate_receive_from (OrenMTMate *self,
                                OrenNCSockaddr *from,
                                OrenNCBuffer *buffer,
                                gboolean *is_close);

void _oren_mtmate_send_close (OrenMTMate *self);

G_END_DECLS

#endif /* __OREN_MT_MATE_H__ */
