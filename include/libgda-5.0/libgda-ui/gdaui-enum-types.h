


#ifndef __LIBGDAUI_ENUM_TYPES_H__
#define __LIBGDAUI_ENUM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS
GType gdaui_basic_form_part_get_type (void);
#define GDAUI_TYPE_BASIC_FORM_PART (gdaui_basic_form_part_get_type())
GType gdaui_data_entry_error_get_type (void);
#define GDAUI_TYPE_DATA_ENTRY_ERROR (gdaui_data_entry_error_get_type())
GType gdaui_data_proxy_write_mode_get_type (void);
#define GDAUI_TYPE_DATA_PROXY_WRITE_MODE (gdaui_data_proxy_write_mode_get_type())
GType gdaui_data_proxy_info_flag_get_type (void);
#define GDAUI_TYPE_DATA_PROXY_INFO_FLAG (gdaui_data_proxy_info_flag_get_type())
GType gdaui_action_mode_get_type (void);
#define GDAUI_TYPE_ACTION_MODE (gdaui_action_mode_get_type())
GType gdaui_action_get_type (void);
#define GDAUI_TYPE_ACTION (gdaui_action_get_type())
GType gdaui_login_mode_get_type (void);
#define GDAUI_TYPE_LOGIN_MODE (gdaui_login_mode_get_type())
G_END_DECLS

#endif /* __LIBGDAUI_ENUM_TYPES_H__ */



