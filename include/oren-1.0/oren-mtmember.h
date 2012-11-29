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
#ifndef __OREN_MT_MEMBER_H__
#define __OREN_MT_MEMBER_H__

#include "oren-mttypes.h"
#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_MTMEMBER (oren_mtmember_get_type())
#define OREN_MTMEMBER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_MTMEMBER, OrenMTMember))
#define OREN_IS_MTMEMBER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_MTMEMBER))
#define OREN_MTMEMBER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_MTMEMBER, OrenMTMemberClass))
#define OREN_IS_MTMEMBER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_MTMEMBER))
#define OREN_MTMEMBER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_MTMEMBER, OrenMTMemberClass))

typedef struct _OrenMTMemberPrivate OrenMTMemberPrivate;
typedef struct _OrenMTMemberClass OrenMTMemberClass;

struct _OrenMTMember {
    GObject parent_instance;
    OrenMTMemberPrivate *priv;
};

struct _OrenMTMemberClass {
    GObjectClass parent_class;
};

GType oren_mtmember_get_type (void) G_GNUC_CONST;

OrenMTMember* oren_mtmember_new (OrenMTSource *source,
                                 const gchar *member_name,
                                 guint32 member_id,
                                 OrenNCSockaddr *observed_addr,
                                 OrenNCSockaddr *local_addr,
                                 guint16 network_type,
                                 guint16 data_seq);

const gchar* oren_mtmember_get_name (OrenMTMember *self);

guint32 oren_mtmember_get_id (OrenMTMember *self);

guint16 oren_mtmember_get_network_type (OrenMTMember *self);

OrenNCSockaddr* oren_mtmember_get_observed_address (OrenMTMember *self);

OrenNCSockaddr* oren_mtmember_get_local_address (OrenMTMember *self);

gboolean oren_mtmember_is_join (OrenMTMember *self);

gboolean oren_mtmember_is_spare (OrenMTMember *self);

void oren_mtmember_set_rtt (OrenMTMember *self, guint rtt);

void oren_mtmember_recv_from (OrenMTMember *self,
                              OrenNCSockaddr *from,
                              guint8 session_id,
                              OrenNCBuffer *buffer);

void _oren_mtmember_set_buffer (OrenMTMember *self,
                                OrenNCBuffer *buffer,
                                gsize head_reserve);

void _oren_mtmember_clear_send (OrenMTMember *self);

void _oren_mtmember_work (OrenMTMember *self);

OrenNCBuffer* _oren_mtmember_new_buffer (OrenMTMember *self);

void _oren_mtmember_send_command (OrenMTMember *self,
                                  OrenNCBuffer *buffer,
                                  gboolean flush);

guint16 _oren_mtmember_enqueue_data (OrenMTMember *self,
                                     OrenNCBuffer *buffer);

void _oren_mtmember_send_s2p (OrenMTMember *self,
                              OrenNCBuffer *buffer);

G_END_DECLS

#endif /* __OREN_MT_MEMBER_H__ */
