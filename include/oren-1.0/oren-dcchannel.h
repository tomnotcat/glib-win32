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
#ifndef __OREN_DC_CHANNEL_H__
#define __OREN_DC_CHANNEL_H__

#include "oren-nchandler.h"
#include "oren-dctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_DCCHANNEL (oren_dcchannel_get_type())
#define OREN_DCCHANNEL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_DCCHANNEL, OrenDCChannel))
#define OREN_IS_DCCHANNEL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_DCCHANNEL))
#define OREN_DCCHANNEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_DCCHANNEL, OrenDCChannelClass))
#define OREN_IS_DCCHANNEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_DCCHANNEL))
#define OREN_DCCHANNEL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_DCCHANNEL, OrenDCChannelClass))

typedef struct _OrenDCChannelPrivate OrenDCChannelPrivate;
typedef struct _OrenDCChannelClass OrenDCChannelClass;

struct _OrenDCChannel {
    OrenNCHandler parent_instance;
    OrenDCChannelPrivate *priv;
};

struct _OrenDCChannelClass {
    OrenNCHandlerClass parent_class;
    gboolean (*auth_user) (OrenDCChannel *self,
                           OrenDCUser *user,
                           OrenNCBuffer *data);
    void (*add_user) (OrenDCChannel *self, OrenDCUser *user);
    void (*remove_user) (OrenDCChannel *self, OrenDCUser *user);
    void (*alone) (OrenDCChannel *self, gboolean alone);
    void (*open) (OrenDCChannel *self);
    void (*close) (OrenDCChannel *self);
    void (*upgrade) (OrenDCChannel *self);
};

/**
 * OrenMulticastFilter:
 * @user: the user
 * @user_data: (closure): user data to pass to the function
 *
 * Function used for filter multicast users.
 *
 * Returns: Whether filter the user or not.
 */
typedef gboolean (*OrenMulticastFilter) (OrenDCUser *user,
                                         gpointer user_data);

GType oren_dcchannel_get_type (void) G_GNUC_CONST;

OrenDCChannel* oren_dcchannel_new (void);

gboolean oren_dcchannel_open (OrenDCChannel *self,
                              OrenDCServer *server,
                              OrenNCReactor *reactor,
                              OrenDCFactory *factory,
                              GPtrArray *address,
                              guint16 port,
                              OrenNCSockaddr *parent_addr,
                              const gchar *channel_name,
                              const gchar *signature,
                              gboolean enable_p2p);

void oren_dcchannel_close (OrenDCChannel *self);

void oren_dcchannel_upgrade (OrenDCChannel *self);

const gchar* oren_dcchannel_get_name (OrenDCChannel *self);

const gchar* oren_dcchannel_get_signature (OrenDCChannel *self);

OrenNCReactor* oren_dcchannel_get_reactor (OrenDCChannel *self);

OrenDCServer* oren_dcchannel_get_server (OrenDCChannel *self);

OrenDCFactory* oren_dcchannel_get_factory (OrenDCChannel *self);

OrenDCClient* oren_dcchannel_get_parent (OrenDCChannel *self);

guint oren_dcchannel_get_user_count (OrenDCChannel *self);

void oren_dcchannel_add_user (OrenDCChannel *self,
                              guint8 protocol_version,
                              gboolean is_channel,
                              const gchar *client_version,
                              const gchar *network_type,
                              const gchar *machine_code,
                              OrenNCSocket *socket,
                              OrenNCSockaddr *from,
                              const gchar *user_name,
                              guint32 login_code,
                              OrenNCBuffer *auth_data);

void oren_dcchannel_remove_user (OrenDCChannel *self,
                                 guint user_id);

void oren_dcchannel_change_parent (OrenDCChannel *self,
                                   OrenNCSockaddr *addr,
                                   gboolean reconnect);

OrenDCUser* oren_dcchannel_get_user (OrenDCChannel *self,
                                     guint user_id);

void oren_dcchannel_get_transfer_status (OrenDCChannel *self,
                                         OrenDCTransferStatus *status);

void oren_dcchannel_freeze (OrenDCChannel *self);

gboolean oren_dcchannel_is_frozen (OrenDCChannel *self);

OrenNCBuffer* oren_dcchannel_make_packet (OrenDCChannel *self,
                                          guint msg);

void oren_dcchannel_multicast (OrenDCChannel *self,
                               OrenDCUser *skip,
                               OrenNCBuffer *buffer,
                               guint max_retry);

void oren_dcchannel_multicast_p2p (OrenDCChannel *self,
                                   OrenDCUser *skip,
                                   OrenNCBuffer *buffer,
                                   guint max_retry);

void oren_dcchannel_multicast_full (OrenDCChannel *self,
                                    OrenDCUser *skip,
                                    OrenNCBuffer *buffer,
                                    OrenMulticastFilter filter,
                                    gpointer user_data,
                                    guint max_retry,
                                    gboolean flush,
                                    gboolean use_p2p);

void _oren_dcchannel_remove_user (OrenDCChannel *self,
                                  OrenDCUser *user);

void _oren_dcchannel_enable_p2p (OrenDCChannel *self,
                                 OrenDCUser *user);

void _oren_dcchannel_trace_route (OrenDCChannel *self,
                                  OrenDCUser *user,
                                  guint32 seq,
                                  const gchar *target,
                                  gint timeout,
                                  gboolean from_parent);

void _oren_dcchannel_route_back (OrenDCChannel *self,
                                 OrenDCUser *from,
                                 guint32 seq,
                                 GSList *routes);

void _oren_dcchannel_send_packet_to (OrenDCChannel *self,
                                     OrenNCSocket *socket,
                                     OrenNCSockaddr *address,
                                     OrenNCBuffer *buffer);

void _oren_dcchannel_send_chgaddr (OrenDCChannel *self,
                                   OrenDCUser *user,
                                   OrenNCSockaddr *addr);

void _oren_dcchannel_add_send_data (OrenDCChannel *self,
                                    guint32 size);

void _oren_dcchannel_add_send_packet (OrenDCChannel *self,
                                      guint32 count);

void _oren_dcchannel_add_send_lost (OrenDCChannel *self,
                                    guint32 count);

gboolean _oren_dcchannel_check_expire (OrenDCChannel *self);

void _oren_dcchannel_async_call (OrenDCChannel *self,
                                 void (*func) (gpointer, gboolean),
                                 gpointer user_data);

G_END_DECLS

#endif /* __OREN_DC_CHANNEL_H__ */
