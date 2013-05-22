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
#ifndef __OREN_GP_USER_H__
#define __OREN_GP_USER_H__

#include "oren-dcuser.h"
#include "oren-gptypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_GPUSER (oren_gpuser_get_type())
#define OREN_GPUSER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_GPUSER, OrenGPUser))
#define OREN_IS_GPUSER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_GPUSER))
#define OREN_GPUSER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_GPUSER, OrenGPUserClass))
#define OREN_IS_GPUSER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_GPUSER))
#define OREN_GPUSER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_GPUSER, OrenGPUserClass))

typedef struct _OrenGPUserPrivate OrenGPUserPrivate;
typedef struct _OrenGPUserClass OrenGPUserClass;

struct _OrenGPUser {
    OrenDCUser parent_instance;
    OrenGPUserPrivate *priv;
};

struct _OrenGPUserClass {
    OrenDCUserClass parent_class;
};

GType oren_gpuser_get_type (void) G_GNUC_CONST;

OrenGPUser* oren_gpuser_new (OrenDCChannel *channel,
                             OrenNCSocket *socket,
                             OrenNCSockaddr *address,
                             OrenNCBuffer *buffer,
                             const gchar *user_name,
                             const gchar *client_version,
                             const gchar *network_type,
                             const gchar *machine_code,
                             guint32 user_id,
                             guint32 login_code,
                             guint protocol_version,
                             gboolean is_channel);

OrenNCBuffer* oren_gpuser_make_protocol (OrenGPUser *self,
                                         const gchar *message);

void oren_gpuser_send_protocol (OrenGPUser *self,
                                OrenNCBuffer *buffer);

void oren_gpuser_set_privilege (OrenGPUser *self,
                                OrenGPUserPrivilege privilege);

OrenGPUserPrivilege oren_gpuser_get_privilege (OrenGPUser *self);

gboolean oren_gpuser_has_privilege (OrenGPUser *self,
                                    OrenGPUserPrivilege privilege);

G_END_DECLS

#endif /* __OREN_GP_USER_H__ */
