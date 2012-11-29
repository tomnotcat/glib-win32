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
#ifndef __OREN_MT_PEER_H__
#define __OREN_MT_PEER_H__

#include "oren-mttypes.h"
#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_MTPEER (oren_mtpeer_get_type())
#define OREN_MTPEER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_MTPEER, OrenMTPeer))
#define OREN_IS_MTPEER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_MTPEER))
#define OREN_MTPEER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_MTPEER, OrenMTPeerClass))
#define OREN_IS_MTPEER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_MTPEER))
#define OREN_MTPEER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_MTPEER, OrenMTPeerClass))

typedef struct _OrenMTPeerPrivate OrenMTPeerPrivate;
typedef struct _OrenMTPeerClass OrenMTPeerClass;

struct _OrenMTPeer {
    GObject parent_instance;
    OrenMTPeerPrivate *priv;
};

struct _OrenMTPeerClass {
    GObjectClass parent_class;
    void (*send_to) (OrenMTPeer *self,
                     OrenNCSockaddr *address,
                     OrenNCBuffer *buffer);
    void (*recv) (OrenMTPeer *self, OrenNCBuffer *buffer);
    void (*join) (OrenMTPeer *self, gboolean join);
};

GType oren_mtpeer_get_type (void) G_GNUC_CONST;

OrenMTPeer* oren_mtpeer_new (const gchar *peer_name,
                             guint32 peer_id,
                             guint16 data_seq,
                             OrenNCSockaddr *source_addr,
                             guint branch_count);

const gchar* oren_mtpeer_get_name (OrenMTPeer *self);

guint32 oren_mtpeer_get_id (OrenMTPeer *self);

guint oren_mtpeer_branch_count (OrenMTPeer *self);

OrenMTBranch* oren_mtpeer_get_branch (OrenMTPeer *self,
                                      guint branch_id);

void oren_mtpeer_set_buffer (OrenMTPeer *self,
                             OrenNCBuffer *buffer,
                             gsize head_reserve);

void oren_mtpeer_set_join (OrenMTPeer *self, gboolean join);

void oren_mtpeer_set_source_rtt (OrenMTPeer *self, guint rtt);

guint oren_mtpeer_get_branch_rtt (OrenMTPeer *self);

void oren_mtpeer_receive_from (OrenMTPeer *self,
                               OrenNCSockaddr *from,
                               OrenNCBuffer *buffer);

void oren_mtpeer_clear_recv (OrenMTPeer *self);

void oren_mtpeer_work (OrenMTPeer *self);

void oren_mtpeer_disconnect (OrenMTPeer *self);

OrenNCBuffer* _oren_mtpeer_new_buffer (OrenMTPeer *self);

void _oren_mtpeer_send_command (OrenMTPeer *self,
                                OrenNCBuffer *buffer,
                                gboolean flush);

OrenNCBuffer* _oren_mtpeer_init_p2pbuf (OrenMTPeer *self,
                                        guint32 mate_id);

void _oren_mtpeer_send_p2pbuf (OrenMTPeer *self,
                               OrenNCSockaddr *addr,
                               OrenNCBuffer *buffer);

void _oren_mtpeer_recv_data (OrenMTPeer *self,
                             OrenNCBuffer *buffer);

G_END_DECLS

#endif /* __OREN_MT_PEER_H__ */
