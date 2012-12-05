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
#ifndef __OREN_DC_LOGIN_H__
#define __OREN_DC_LOGIN_H__

#include "oren-nchandler.h"
#include "oren-cmtypes.h"
#include "oren-dctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_DCLOGIN (oren_dclogin_get_type())
#define OREN_DCLOGIN(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_DCLOGIN, OrenDCLogin))
#define OREN_IS_DCLOGIN(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_DCLOGIN))
#define OREN_DCLOGIN_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_DCLOGIN, OrenDCLoginClass))
#define OREN_IS_DCLOGIN_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_DCLOGIN))
#define OREN_DCLOGIN_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_DCLOGIN, OrenDCLoginClass))

typedef struct _OrenDCLoginPrivate OrenDCLoginPrivate;
typedef struct _OrenDCLoginClass OrenDCLoginClass;

struct _OrenDCLogin {
    OrenNCHandler parent_instance;
    OrenDCLoginPrivate *priv;
};

struct _OrenDCLoginClass {
    OrenNCHandlerClass parent_class;
    void (*choose) (OrenDCLogin *self, OrenCMItem *item);
    void (*login) (OrenDCLogin *self, OrenDCLoginResult result);
    void (*logout) (OrenDCLogin *self, OrenDCLogoutReason reason);
};

GType oren_dclogin_get_type (void) G_GNUC_CONST;

OrenDCLogin* oren_dclogin_new (void);

gboolean oren_dclogin_open (OrenDCLogin *self,
                            OrenNCReactor *reactor,
                            OrenCMClient *cmclient,
                            OrenDCClient *dcclient,
                            OrenDCFactory *factory,
                            const gchar *signature,
                            gboolean statistics);

void oren_dclogin_close (OrenDCLogin *self);

void oren_dclogin_login (OrenDCLogin *self,
                         OrenNCSockaddr *address,
                         gboolean is_cmaddr,
                         const gchar *group_name,
                         guint slave_count,
                         const gchar *channel_name,
                         const gchar *user_name,
                         const gchar *network_type,
                         const gchar *place);

void oren_dclogin_logout (OrenDCLogin *self);

OrenNCOnlineState oren_dclogin_get_state (OrenDCLogin *self);

OrenCMClient* oren_dclogin_get_cmclient (OrenDCLogin *self);

OrenDCClient* oren_dclogin_get_dcclient (OrenDCLogin *self);

#define oren_dclogin_is_online(_self) \
    (oren_dclogin_get_state (_self) == OREN_NCONLINE_STATE_ONLINE)

#define oren_dclogin_is_offline(_self) \
    (oren_dclogin_get_state (_self) == OREN_NCONLINE_STATE_OFFLINE)

OrenNCSockaddr* oren_dclogin_parse_address (const gchar *address,
                                            gboolean *is_cmaddr);

G_END_DECLS

#endif /* __OREN_DC_LOGIN_H__ */
