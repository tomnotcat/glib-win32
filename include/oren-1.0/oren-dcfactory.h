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
#ifndef __OREN_DC_FACTORY_H__
#define __OREN_DC_FACTORY_H__

#include "oren-nctypes.h"
#include "oren-dctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_DCFACTORY (oren_dcfactory_get_type())
#define OREN_DCFACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_DCFACTORY, OrenDCFactory))
#define OREN_IS_DCFACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_DCFACTORY))
#define OREN_DCFACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_DCFACTORY, OrenDCFactoryClass))
#define OREN_IS_DCFACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_DCFACTORY))
#define OREN_DCFACTORY_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_DCFACTORY, OrenDCFactoryClass))

typedef struct _OrenDCFactoryPrivate OrenDCFactoryPrivate;
typedef struct _OrenDCFactoryClass OrenDCFactoryClass;

struct _OrenDCFactory {
    GObject parent_instance;
};

struct _OrenDCFactoryClass {
    GObjectClass parent_class;
    OrenDCChannel* (*make_channel) (OrenDCFactory *self,
                                    OrenDCServer *server,
                                    OrenNCReactor *reactor,
                                    GPtrArray *address,
                                    OrenNCSockaddr *parent_addr,
                                    const gchar *channel_name,
                                    gboolean enable_p2p);
    OrenDCUser* (*make_user) (OrenDCFactory *self,
                              OrenDCChannel *channel,
                              OrenNCSocket *socket,
                              OrenNCSockaddr *address,
                              OrenNCBuffer *buffer,
                              const gchar *user_name,
                              const gchar *client_version,
                              const gchar *network_type,
                              const gchar *machine_code,
                              guint user_id,
                              guint login_code,
                              guint protocol_version,
                              gboolean is_channel);
    OrenDCClient* (*make_client) (OrenDCFactory *self,
                                  OrenNCReactor *reactor,
                                  const gchar *signature,
                                  gboolean statistics);
    void (*channel_made) (OrenDCFactory *self, OrenDCChannel *channel);
    void (*user_made) (OrenDCFactory *self, OrenDCUser *user);
    void (*client_made) (OrenDCFactory *self, OrenDCClient *client);
    void (*close) (OrenDCFactory *self);
};

GType oren_dcfactory_get_type (void) G_GNUC_CONST;

OrenDCFactory* oren_dcfactory_new (void);

OrenDCChannel* oren_dcfactory_make_channel (OrenDCFactory *self,
                                            OrenDCServer *server,
                                            OrenNCReactor *reactor,
                                            GPtrArray *address,
                                            OrenNCSockaddr *parent_addr,
                                            const gchar *channel_name,
                                            gboolean enable_p2p);

OrenDCUser* oren_dcfactory_make_user (OrenDCFactory *self,
                                      OrenDCChannel *channel,
                                      OrenNCSocket *socket,
                                      OrenNCSockaddr *address,
                                      OrenNCBuffer *buffer,
                                      const gchar *user_name,
                                      const gchar *client_version,
                                      const gchar *network_type,
                                      const gchar *machine_code,
                                      guint user_id,
                                      guint login_code,
                                      guint protocol_version,
                                      gboolean is_channel);

OrenDCClient* oren_dcfactory_make_client (OrenDCFactory *self,
                                          OrenNCReactor *reactor,
                                          const gchar *signature,
                                          gboolean statistics);

G_END_DECLS

#endif /* __OREN_DC_FACTORY_H__ */
