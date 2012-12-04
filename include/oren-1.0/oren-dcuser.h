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
#ifndef __OREN_DC_USER_H__
#define __OREN_DC_USER_H__

#include "oren-nctypes.h"
#include "oren-dctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_DCUSER (oren_dcuser_get_type())
#define OREN_DCUSER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_DCUSER, OrenDCUser))
#define OREN_IS_DCUSER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_DCUSER))
#define OREN_DCUSER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_DCUSER, OrenDCUserClass))
#define OREN_IS_DCUSER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_DCUSER))
#define OREN_DCUSER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_DCUSER, OrenDCUserClass))

typedef struct _OrenDCUserPrivate OrenDCUserPrivate;
typedef struct _OrenDCUserClass OrenDCUserClass;

struct timeval;

struct _OrenDCUser {
    GObject parent_instance;
    OrenDCUserPrivate *priv;
};

struct _OrenDCUserClass {
    GObjectClass parent_class;
    void (*message) (OrenDCUser *self, guint message, OrenNCBuffer *buffer);
    void (*data) (OrenDCUser *self, OrenNCBuffer *buffer);
    void (*p2p) (OrenDCUser *self, gboolean enable);
};

GType oren_dcuser_get_type (void) G_GNUC_CONST;

OrenDCUser* oren_dcuser_new (OrenDCChannel *channel,
                             OrenNCSockaddr *address,
                             OrenNCBuffer *buffer,
                             const gchar *user_name,
                             const gchar *client_version,
                             const gchar *network_type,
                             guint32 user_id,
                             guint32 login_code,
                             guint protocol_version);

OrenNCBuffer* oren_dcuser_make_data (OrenDCUser *self);

OrenNCBuffer* oren_dcuser_make_message (OrenDCUser *self,
                                        guint message);

gboolean oren_dcuser_send_packet (OrenDCUser *self,
                                  OrenNCBuffer *buffer);

void oren_dcuser_clear_buffer (OrenDCUser *self);

OrenDCChannel* oren_dcuser_get_channel (OrenDCUser *self);

guint32 oren_dcuser_get_id (OrenDCUser *self);

guint32 oren_dcuser_get_login_code (OrenDCUser *self);

guint oren_dcuser_protocol_version (OrenDCUser *self);

const gchar* oren_dcuser_client_version (OrenDCUser *self);

const gchar* oren_dcuser_network_type (OrenDCUser *self);

const gchar* oren_dcuser_get_name (OrenDCUser *self);

gboolean oren_dcuser_enable_p2p (OrenDCUser *self);

void oren_dcuser_set_address (OrenDCUser *self,
                              OrenNCSockaddr *addr);

OrenNCSockaddr* oren_dcuser_get_address (OrenDCUser *self);

gboolean _oren_dcuser_send (OrenDCUser *self,
                            OrenNCBuffer *buffer,
                            gboolean flush,
                            gboolean statistics);

gboolean _oren_dcuser_recv (OrenDCUser *self,
                            OrenNCSockaddr *from,
                            guint session_id,
                            OrenNCBuffer *buffer);

gboolean _oren_dcuser_work (OrenDCUser *self,
                            struct timeval *tv,
                            guint timeout);

void _oren_dcuser_teardown (OrenDCUser *self);

G_END_DECLS

#endif /* __OREN_DC_USER_H__ */
