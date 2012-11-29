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
#ifndef __OREN_SM_USER_H__
#define __OREN_SM_USER_H__

#include "oren-dcuser.h"
#include "oren-smtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_SMUSER (oren_smuser_get_type())
#define OREN_SMUSER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_SMUSER, OrenSMUser))
#define OREN_IS_SMUSER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_SMUSER))
#define OREN_SMUSER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_SMUSER, OrenSMUserClass))
#define OREN_IS_SMUSER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_SMUSER))
#define OREN_SMUSER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_SMUSER, OrenSMUserClass))

typedef struct _OrenSMUserPrivate OrenSMUserPrivate;
typedef struct _OrenSMUserClass OrenSMUserClass;

struct _OrenSMUser {
    OrenDCUser parent_instance;
    OrenSMUserPrivate *priv;
};

struct _OrenSMUserClass {
    OrenDCUserClass parent_class;
};

GType oren_smuser_get_type (void) G_GNUC_CONST;

OrenSMUser* oren_smuser_new (OrenDCChannel *channel,
                             OrenNCSockaddr *address,
                             OrenNCBuffer *buffer,
                             const gchar *user_name,
                             const gchar *client_version,
                             const gchar *network_type,
                             guint32 user_id,
                             guint32 login_code,
                             guint protocol_version);

gboolean oren_smuser_is_source (OrenSMUser *self);

gboolean oren_smuser_accept_data (OrenSMUser *self);

#define oren_smuser_get_id(_self) \
    oren_dcuser_get_id (OREN_DCUSER (_self))

#define oren_smuser_get_login_code(_self) \
    oren_dcuser_get_login_code (OREN_DCUSER (_self))

#define oren_smuser_protocol_version(_self) \
    oren_dcuser_protocol_version (OREN_DCUSER (_self))

#define oren_smuser_client_version(_self) \
    oren_dcuser_client_version (OREN_DCUSER (_self))

#define oren_smuser_network_type(_self) \
    oren_dcuser_network_type (OREN_DCUSER (_self))

#define oren_smuser_get_name(_self) \
    oren_dcuser_get_name (OREN_DCUSER (_self))

#define oren_smuser_get_address(_self) \
    oren_dcuser_get_address (OREN_DCUSER (_self))

G_END_DECLS

#endif /* __OREN_SM_USER_H__ */
