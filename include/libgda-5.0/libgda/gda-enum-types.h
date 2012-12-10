


#ifndef __LIBGDA_ENUM_TYPES_H__
#define __LIBGDA_ENUM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS
GType gda_batch_error_get_type (void);
#define GDA_TYPE_BATCH_ERROR (gda_batch_error_get_type())
GType gda_config_error_get_type (void);
#define GDA_TYPE_CONFIG_ERROR (gda_config_error_get_type())
GType gda_connection_event_type_get_type (void);
#define GDA_TYPE_CONNECTION_EVENT_TYPE (gda_connection_event_type_get_type())
GType gda_connection_event_code_get_type (void);
#define GDA_TYPE_CONNECTION_EVENT_CODE (gda_connection_event_code_get_type())
GType gda_connection_error_get_type (void);
#define GDA_TYPE_CONNECTION_ERROR (gda_connection_error_get_type())
GType gda_connection_options_get_type (void);
#define GDA_TYPE_CONNECTION_OPTIONS (gda_connection_options_get_type())
GType gda_connection_feature_get_type (void);
#define GDA_TYPE_CONNECTION_FEATURE (gda_connection_feature_get_type())
GType gda_connection_meta_type_get_type (void);
#define GDA_TYPE_CONNECTION_META_TYPE (gda_connection_meta_type_get_type())
GType gda_data_comparator_error_get_type (void);
#define GDA_TYPE_DATA_COMPARATOR_ERROR (gda_data_comparator_error_get_type())
GType gda_diff_type_get_type (void);
#define GDA_TYPE_DIFF_TYPE (gda_diff_type_get_type())
GType gda_data_model_access_flags_get_type (void);
#define GDA_TYPE_DATA_MODEL_ACCESS_FLAGS (gda_data_model_access_flags_get_type())
GType gda_data_model_hint_get_type (void);
#define GDA_TYPE_DATA_MODEL_HINT (gda_data_model_hint_get_type())
GType gda_data_model_io_format_get_type (void);
#define GDA_TYPE_DATA_MODEL_IO_FORMAT (gda_data_model_io_format_get_type())
GType gda_data_model_error_get_type (void);
#define GDA_TYPE_DATA_MODEL_ERROR (gda_data_model_error_get_type())
GType gda_ldap_search_scope_get_type (void);
#define GDA_TYPE_LDAP_SEARCH_SCOPE (gda_ldap_search_scope_get_type())
GType gda_data_model_iter_error_get_type (void);
#define GDA_TYPE_DATA_MODEL_ITER_ERROR (gda_data_model_iter_error_get_type())
GType gda_data_proxy_error_get_type (void);
#define GDA_TYPE_DATA_PROXY_ERROR (gda_data_proxy_error_get_type())
GType gda_data_select_error_get_type (void);
#define GDA_TYPE_DATA_SELECT_ERROR (gda_data_select_error_get_type())
GType gda_data_select_condition_type_get_type (void);
#define GDA_TYPE_DATA_SELECT_CONDITION_TYPE (gda_data_select_condition_type_get_type())
GType gda_transaction_isolation_get_type (void);
#define GDA_TYPE_TRANSACTION_ISOLATION (gda_transaction_isolation_get_type())
GType gda_value_attribute_get_type (void);
#define GDA_TYPE_VALUE_ATTRIBUTE (gda_value_attribute_get_type())
GType gda_sql_identifier_style_get_type (void);
#define GDA_TYPE_SQL_IDENTIFIER_STYLE (gda_sql_identifier_style_get_type())
GType gda_holder_error_get_type (void);
#define GDA_TYPE_HOLDER_ERROR (gda_holder_error_get_type())
GType gda_meta_store_error_get_type (void);
#define GDA_TYPE_META_STORE_ERROR (gda_meta_store_error_get_type())
GType gda_meta_store_change_type_get_type (void);
#define GDA_TYPE_META_STORE_CHANGE_TYPE (gda_meta_store_change_type_get_type())
GType gda_meta_struct_error_get_type (void);
#define GDA_TYPE_META_STRUCT_ERROR (gda_meta_struct_error_get_type())
GType gda_meta_db_object_type_get_type (void);
#define GDA_TYPE_META_DB_OBJECT_TYPE (gda_meta_db_object_type_get_type())
GType gda_meta_struct_feature_get_type (void);
#define GDA_TYPE_META_STRUCT_FEATURE (gda_meta_struct_feature_get_type())
GType gda_meta_sort_type_get_type (void);
#define GDA_TYPE_META_SORT_TYPE (gda_meta_sort_type_get_type())
GType gda_meta_foreign_key_policy_get_type (void);
#define GDA_TYPE_META_FOREIGN_KEY_POLICY (gda_meta_foreign_key_policy_get_type())
GType gda_meta_graph_info_get_type (void);
#define GDA_TYPE_META_GRAPH_INFO (gda_meta_graph_info_get_type())
GType gda_set_error_get_type (void);
#define GDA_TYPE_SET_ERROR (gda_set_error_get_type())
GType gda_server_operation_type_get_type (void);
#define GDA_TYPE_SERVER_OPERATION_TYPE (gda_server_operation_type_get_type())
GType gda_server_operation_error_get_type (void);
#define GDA_TYPE_SERVER_OPERATION_ERROR (gda_server_operation_error_get_type())
GType gda_server_operation_create_table_flag_get_type (void);
#define GDA_TYPE_SERVER_OPERATION_CREATE_TABLE_FLAG (gda_server_operation_create_table_flag_get_type())
GType gda_server_operation_node_type_get_type (void);
#define GDA_TYPE_SERVER_OPERATION_NODE_TYPE (gda_server_operation_node_type_get_type())
GType gda_server_operation_node_status_get_type (void);
#define GDA_TYPE_SERVER_OPERATION_NODE_STATUS (gda_server_operation_node_status_get_type())
GType gda_server_provider_error_get_type (void);
#define GDA_TYPE_SERVER_PROVIDER_ERROR (gda_server_provider_error_get_type())
GType gda_statement_error_get_type (void);
#define GDA_TYPE_STATEMENT_ERROR (gda_statement_error_get_type())
GType gda_statement_model_usage_get_type (void);
#define GDA_TYPE_STATEMENT_MODEL_USAGE (gda_statement_model_usage_get_type())
GType gda_statement_sql_flag_get_type (void);
#define GDA_TYPE_STATEMENT_SQL_FLAG (gda_statement_sql_flag_get_type())
GType gda_sql_builder_error_get_type (void);
#define GDA_TYPE_SQL_BUILDER_ERROR (gda_sql_builder_error_get_type())
GType gda_transaction_status_event_type_get_type (void);
#define GDA_TYPE_TRANSACTION_STATUS_EVENT_TYPE (gda_transaction_status_event_type_get_type())
GType gda_transaction_status_state_get_type (void);
#define GDA_TYPE_TRANSACTION_STATUS_STATE (gda_transaction_status_state_get_type())
GType gda_tree_error_get_type (void);
#define GDA_TYPE_TREE_ERROR (gda_tree_error_get_type())
GType gda_tree_node_error_get_type (void);
#define GDA_TYPE_TREE_NODE_ERROR (gda_tree_node_error_get_type())
GType gda_tree_manager_error_get_type (void);
#define GDA_TYPE_TREE_MANAGER_ERROR (gda_tree_manager_error_get_type())
GType gda_xa_transaction_error_get_type (void);
#define GDA_TYPE_XA_TRANSACTION_ERROR (gda_xa_transaction_error_get_type())
GType gda_data_pivot_error_get_type (void);
#define GDA_TYPE_DATA_PIVOT_ERROR (gda_data_pivot_error_get_type())
GType gda_data_pivot_aggregate_get_type (void);
#define GDA_TYPE_DATA_PIVOT_AGGREGATE (gda_data_pivot_aggregate_get_type())
GType gda_data_pivot_field_type_get_type (void);
#define GDA_TYPE_DATA_PIVOT_FIELD_TYPE (gda_data_pivot_field_type_get_type())
G_END_DECLS

#endif /* __LIBGDA_ENUM_TYPES_H__ */



