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
#ifndef __OREN_DC_CLIENT_H__
#define __OREN_DC_CLIENT_H__

#include "oren-nchandler.h"
#include "oren-cmtypes.h"
#include "oren-mttypes.h"
#include "oren-dctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_DCCLIENT (oren_dcclient_get_type())
#define OREN_DCCLIENT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_DCCLIENT, OrenDCClient))
#define OREN_IS_DCCLIENT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_DCCLIENT))
#define OREN_DCCLIENT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_DCCLIENT, OrenDCClientClass))
#define OREN_IS_DCCLIENT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_DCCLIENT))
#define OREN_DCCLIENT_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_DCCLIENT, OrenDCClientClass))

typedef struct _OrenDCClientPrivate OrenDCClientPrivate;
typedef struct _OrenDCClientClass OrenDCClientClass;

struct _OrenDCClient {
    OrenNCHandler parent_instance;
    OrenDCClientPrivate *priv;
};

struct _OrenDCClientClass {
    OrenNCHandlerClass parent_class;
    void (*ping) (OrenDCClient *self, OrenDCPingResult *result);
    void (*login) (OrenDCClient *self, OrenDCLoginResult result);
    void (*logout) (OrenDCClient *self, OrenDCLogoutReason reason);
    void (*packet) (OrenDCClient *self, guint msg, OrenNCBuffer *buffer);
    void (*alone) (OrenDCClient *self, gboolean alone);
    void (*p2p) (OrenDCClient *self, gboolean enable);
    void (*work) (OrenDCClient *self);
    void (*close) (OrenDCClient *self);
};

struct _OrenDCTransferStatus {
    guint32 rtt;        /* Round-Trip Time (ms) */
    guint32 generate;   /* Data generate rate (bps) */
    guint32 send_data;  /* Data send rate (bps) */
    guint32 recv_data;  /* Data receive rate (bps) */
    guint32 send_pkt;   /* Packet send rate (pps) */
    guint32 recv_pkt;   /* Packet receive rate (pps) */
    guint32 send_retry; /* Data send retry (pps) */
    guint32 recv_retry; /* Data receive retry (pps) */
    guint32 send_lost;  /* Packet send lost (pps) */
    guint32 recv_lost;  /* Packet receive lost (pps) */
    guint32 duplicate;  /* Packet duplicate received (pps) */
};

GType oren_dcclient_get_type (void) G_GNUC_CONST;

OrenDCClient* oren_dcclient_new (void);

gboolean oren_dcclient_open (OrenDCClient *self,
                             OrenNCReactor *reactor,
                             const gchar *signature,
                             gboolean statistics);

void oren_dcclient_close (OrenDCClient *self);

void oren_dcclient_ping (OrenDCClient *self,
                         OrenCMItem *item,
                         gint timeout);

void oren_dcclient_login (OrenDCClient *self,
                          guint32 login_time,
                          OrenNCSockaddr *address,
                          const gchar *channel_name,
                          const gchar *user_name,
                          const gchar *network_type);

void oren_dcclient_logout (OrenDCClient *self);

OrenNCOnlineState oren_dcclient_get_state (OrenDCClient *self);

void oren_dcclient_enable_p2p (OrenDCClient *self,
                               gboolean enable);

OrenNCBuffer* oren_dcclient_make_packet (OrenDCClient *self,
                                         guint msg);

void oren_dcclient_send_packet (OrenDCClient *self,
                                OrenNCBuffer *buffer,
                                guint max_retry);

const gchar* oren_dcclient_server_name (OrenDCClient *self);

const gchar* oren_dcclient_server_version (OrenDCClient *self);

const gchar* oren_dcclient_get_signature (OrenDCClient *self);

const gchar* oren_dcclient_get_channel_name (OrenDCClient *self);

const gchar* oren_dcclient_get_user_name (OrenDCClient *self);

guint32 oren_dcclient_get_user_id (OrenDCClient *self);

gboolean oren_dcclient_is_alone (OrenDCClient *self);

void oren_dcclient_get_transfer_status (OrenDCClient *self,
                                        OrenDCTransferStatus *status);

void _oren_dcclient_real_enable_p2p (OrenDCClient *self,
                                     gboolean enable);

gboolean _oren_dcclient_real_send (OrenDCClient *self,
                                   OrenNCBuffer *buffer,
                                   guint max_retry,
                                   gboolean flush);

OrenMTPeer* _oren_dcclient_get_mtpeer (OrenDCClient *self);

void _oren_dcclient_async_call (OrenDCClient *self,
                                void (*func) (gpointer, gboolean),
                                gpointer user_data);

void _oren_dcclient_set_test_params (OrenDCClient *self,
                                     gint send_lost,
                                     gint recv_lost);

void _oren_dcclient_set_test_socket (OrenDCClient *self,
                                     OrenNCSocket *socket);

#define oren_dcclient_is_online(_self) \
    (oren_dcclient_get_state (_self) == OREN_NCONLINE_STATE_ONLINE)

#define oren_dcclient_is_offline(_self) \
    (oren_dcclient_get_state (_self) == OREN_NCONLINE_STATE_OFFLINE)

G_END_DECLS

#endif /* __OREN_DC_CLIENT_H__ */
