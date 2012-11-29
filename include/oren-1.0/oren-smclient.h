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
#ifndef __OREN_SM_CLIENT_H__
#define __OREN_SM_CLIENT_H__

#include "oren-dcclient.h"
#include "oren-smtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_SMCLIENT (oren_smclient_get_type())
#define OREN_SMCLIENT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_SMCLIENT, OrenSMClient))
#define OREN_IS_SMCLIENT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_SMCLIENT))
#define OREN_SMCLIENT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_SMCLIENT, OrenSMClientClass))
#define OREN_IS_SMCLIENT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_SMCLIENT))
#define OREN_SMCLIENT_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_SMCLIENT, OrenSMClientClass))

typedef struct _OrenSMClientPrivate OrenSMClientPrivate;
typedef struct _OrenSMClientClass OrenSMClientClass;

struct _OrenSMClient {
    OrenDCClient parent_instance;
    OrenSMClientPrivate *priv;
};

struct _OrenSMClientClass {
    OrenDCClientClass parent_class;
    void (*source) (OrenSMClient *self, OrenNCBuffer *param);
    void (*accept) (OrenSMClient *self, gboolean accept);
    void (*media_format) (OrenSMClient *self, OrenNCBuffer *format);
    void (*media_data) (OrenSMClient *self, OrenNCBuffer *buffer);
};

GType oren_smclient_get_type (void) G_GNUC_CONST;

OrenSMClient* oren_smclient_new (void);

gboolean oren_smclient_open (OrenSMClient *self,
                             OrenNCReactor *reactor,
                             const gchar *signature,
                             gboolean statistics,
                             gboolean pack_data);

void oren_smclient_set_source (OrenSMClient *self,
                               gconstpointer param,
                               gsize size);

void oren_smclient_send_format (OrenSMClient *self,
                                gconstpointer data,
                                gsize size);

void oren_smclient_send_data (OrenSMClient *self,
                              gconstpointer data,
                              gsize size);

void oren_smclient_accept_data (OrenSMClient *self,
                                gboolean accept);

const gchar* oren_smclient_get_source_name (OrenSMClient *self);

guint32 oren_smclient_get_source_id (OrenSMClient *self);

gboolean oren_smclient_is_source (OrenSMClient *self);

#define oren_smclient_close(_self) \
    oren_dcclient_close (OREN_DCCLIENT (_self))

#define oren_smclient_ping(_self, _info, _timeout) \
    oren_dcclient_ping (OREN_DCCLIENT (_self), _info, _timeout)

#define oren_smclient_login(_self, _login_time, _address, \
                            _channel_name, _user_name, _network_type)  \
    oren_dcclient_login (OREN_DCCLIENT (_self), _login_time, _address, \
                         _channel_name, _user_name, _network_type)

#define oren_smclient_logout(_self) \
    oren_dcclient_logout (OREN_DCCLIENT (_self))

#define oren_smclient_get_state(_self) \
    oren_dcclient_get_state (OREN_DCCLIENT (_self))

#define oren_smclient_set_quality(_self, _send, _receive) \
    oren_dcclient_set_quality (OREN_DCCLIENT (_self), _send, _receive)

#define oren_smclient_enable_p2p(_self, _enable) \
    oren_dcclient_enable_p2p (OREN_DCCLIENT (_self), _enable)

#define oren_smclient_get_mtpeer(_self) \
    _oren_dcclient_get_mtpeer (OREN_DCCLIENT (_self))

#define oren_smclient_server_name(_self) \
    oren_dcclient_server_name (OREN_DCCLIENT (_self))

#define oren_smclient_server_version(_self) \
    oren_dcclient_server_version (OREN_DCCLIENT (_self))

#define oren_smclient_get_signature(_self) \
    oren_dcclient_get_signature (OREN_DCCLIENT (_self))

#define oren_smclient_get_channel_name(_self) \
    oren_dcclient_get_channel_name (OREN_DCCLIENT (_self))

#define oren_smclient_get_user_name(_self) \
    oren_dcclient_get_user_name (OREN_DCCLIENT (_self))

#define oren_smclient_get_user_id(_self) \
    oren_dcclient_get_user_id (OREN_DCCLIENT (_self))

#define oren_smclient_is_alone(_self) \
    oren_dcclient_is_alone (OREN_DCCLIENT (_self))

#define oren_smclient_get_transfer_status(_self, _status) \
    oren_dcclient_get_transfer_status (OREN_DCCLIENT (_self), _status)

#define oren_smclient_set_test_params(_self, _send_lost, _recv_lost) \
    oren_dcclient_set_test_params (OREN_DCCLIENT (_self), \
                                   _send_lost, _recv_lost)

#define oren_smclient_is_online(_self) \
    oren_dcclient_is_online (OREN_DCCLIENT (_self))

#define oren_smclient_is_offline(_self) \
    oren_dcclient_is_offline (OREN_DCCLIENT (_self))

G_END_DECLS

#endif /* __OREN_SM_CLIENT_H__ */
