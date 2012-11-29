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
#ifndef __OREN_NC_SESSION_H__
#define __OREN_NC_SESSION_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCSESSION (oren_ncsession_get_type())
#define OREN_NCSESSION(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCSESSION, OrenNCSession))
#define OREN_IS_NCSESSION(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCSESSION))
#define OREN_NCSESSION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCSESSION, OrenNCSessionClass))
#define OREN_IS_NCSESSION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCSESSION))
#define OREN_NCSESSION_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCSESSION, OrenNCSessionClass))

#define OREN_NCSESSION_HEAD_SIZE (3)

/**
 * OREN_NCSESSION_PING_TIMEOUT:
 * Pass as RTT to ping signal when ping timeout.
 *
 * Value: -1
 */
#define OREN_NCSESSION_PING_TIMEOUT (-1)

typedef struct _OrenNCSessionPrivate OrenNCSessionPrivate;
typedef struct _OrenNCSessionClass OrenNCSessionClass;

struct _OrenNCSession {
    GObject parent_instance;
    OrenNCSessionPrivate *priv;
};

struct _OrenNCSessionClass {
    GObjectClass parent_class;
    void (*send) (OrenNCSession *self, OrenNCBuffer *buffer);
    void (*recv) (OrenNCSession *self, OrenNCBuffer *buffer);
    void (*full) (OrenNCSession *self, OrenNCSessionFull type);
    void (*send_retry) (OrenNCSession *self, gint count);
    void (*recv_retry) (OrenNCSession *self, gint count);
    void (*duplicate) (OrenNCSession *self, OrenNCBuffer *buffer);
    void (*ping) (OrenNCSession *self, gint rtt);
    void (*send_lost) (OrenNCSession *self, gint count);
    void (*recv_lost) (OrenNCSession *self, gint count);
    void (*recv_ack) (OrenNCSession *self, gint count);
};

GType oren_ncsession_get_type (void) G_GNUC_CONST;

OrenNCSession* oren_ncsession_new (guint16 init_seq,
                                   gboolean full_fail,
                                   gboolean fifo);

void oren_ncsession_reset (OrenNCSession *self,
                           guint16 init_seq,
                           gboolean reset_pps);

void oren_ncsession_clear_send (OrenNCSession *self);

void oren_ncsession_clear_recv (OrenNCSession *self);

void oren_ncsession_set_name (OrenNCSession *self,
                              const gchar *name);

void oren_ncsession_set_buffer (OrenNCSession *self,
                                OrenNCBuffer *buffer,
                                gsize head_reserve);

OrenNCBuffer* oren_ncsession_get_buffer (OrenNCSession *self);

void oren_ncsession_set_max_resend (OrenNCSession *self,
                                    guint resend);

guint oren_ncsession_get_max_resend (OrenNCSession *self);

void oren_ncsession_set_pps (OrenNCSession *self, guint pps);

guint oren_ncsession_get_pps (OrenNCSession *self);

void oren_ncsession_set_rtt (OrenNCSession *self, guint rtt);

guint oren_ncsession_get_rtt (OrenNCSession *self);

guint16 oren_ncsession_enqueue (OrenNCSession *self,
                                OrenNCBuffer *buffer);

gboolean oren_ncsession_send (OrenNCSession *self,
                              OrenNCBuffer *buffer);

gboolean oren_ncsession_recv (OrenNCSession *self,
                              OrenNCBuffer *buffer);

void oren_ncsession_work (OrenNCSession *self,
                          OrenNCSessionWork flags);

void _oren_ncsession_write_data_head (OrenNCBuffer *buffer,
                                      guint16 seq);

G_END_DECLS

#endif /* __OREN_NC_SESSION_H__ */
