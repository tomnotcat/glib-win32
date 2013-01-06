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
    void (*start) (OrenSMClient *self,
                   guint line_number,
                   OrenNCBuffer *param);
    void (*stop) (OrenSMClient *self,
                  guint line_number);
    void (*refuse) (OrenSMClient *self,
                    guint line_number,
                    guint data_types);
    void (*meta) (OrenSMClient *self,
                  guint line_number,
                  OrenNCBuffer *meta);
    void (*data) (OrenSMClient *self,
                  guint line_number,
                  guint data_type,
                  guint max_retry,
                  OrenNCBuffer *buffer);
};

GType oren_smclient_get_type (void) G_GNUC_CONST;

OrenSMClient* oren_smclient_new (gboolean pack_data);

void oren_smclient_start_line (OrenSMClient *self,
                               guint line_number,
                               gconstpointer param,
                               gsize size);

void oren_smclient_stop_line (OrenSMClient *self,
                              guint line_number);

void oren_smclient_send_meta (OrenSMClient *self,
                              guint line_number,
                              gconstpointer data,
                              gsize size);

void oren_smclient_send_data (OrenSMClient *self,
                              guint line_number,
                              guint data_type,
                              gconstpointer data,
                              gsize size,
                              guint max_retry);

void oren_smclient_send_refuse (OrenSMClient *self,
                                guint line_number,
                                guint data_types);

gchar* oren_smclient_dup_source_name (OrenSMClient *self,
                                      guint line_number);

guint32 oren_smclient_get_source_id (OrenSMClient *self,
                                     guint line_number);

gboolean oren_smclient_is_source (OrenSMClient *self,
                                  guint line_number);

G_END_DECLS

#endif /* __OREN_SM_CLIENT_H__ */
