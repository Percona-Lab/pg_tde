/* contrib/pg_tde/pg_tde--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_tde" to load this file. \quit

-- Key Provider Management
CREATE FUNCTION pg_tde_add_key_provider_internal(provider_type VARCHAR(10), provider_name VARCHAR(128), options JSON)
RETURNS INT
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE OR REPLACE FUNCTION pg_tde_add_key_provider(provider_type VARCHAR(10), provider_name VARCHAR(128), options JSON)
RETURNS INT
AS $$
    SELECT pg_tde_add_key_provider_internal(provider_type, provider_name, options);
$$
LANGUAGE SQL;

CREATE OR REPLACE FUNCTION pg_tde_add_key_provider_file(provider_name VARCHAR(128), file_path TEXT)
RETURNS INT
AS $$
-- JSON keys in the options must be matched to the keys in
-- load_file_keyring_provider_options function.

    SELECT pg_tde_add_key_provider('file', provider_name,
                json_object('type' VALUE 'file', 'path' VALUE COALESCE(file_path, '')));
$$
LANGUAGE SQL;

CREATE OR REPLACE FUNCTION pg_tde_add_key_provider_file(provider_name VARCHAR(128), file_path JSON)
RETURNS INT
AS $$
-- JSON keys in the options must be matched to the keys in
-- load_file_keyring_provider_options function.

    SELECT pg_tde_add_key_provider('file', provider_name,
                json_object('type' VALUE 'file', 'path' VALUE file_path));
$$
LANGUAGE SQL;

CREATE OR REPLACE FUNCTION pg_tde_add_key_provider_vault_v2(provider_name VARCHAR(128),
                                                        vault_token TEXT,
                                                        vault_url TEXT,
                                                        vault_mount_path TEXT,
                                                        vault_ca_path TEXT)
RETURNS INT
AS $$
-- JSON keys in the options must be matched to the keys in
-- load_vaultV2_keyring_provider_options function.
    SELECT pg_tde_add_key_provider('vault-v2', provider_name,
                            json_object('type' VALUE 'vault-v2',
                            'url' VALUE COALESCE(vault_url,''),
                            'token' VALUE COALESCE(vault_token,''),
                            'mountPath' VALUE COALESCE(vault_mount_path,''),
                            'caPath' VALUE COALESCE(vault_ca_path,'')));
$$
LANGUAGE SQL;

-- Table access method
CREATE FUNCTION pg_tdeam_basic_handler(internal)
RETURNS table_am_handler
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_tde_is_encrypted(table_name VARCHAR)
RETURNS boolean
AS $$
SELECT EXISTS (
    SELECT 1
    FROM   pg_catalog.pg_class
    WHERE  relname = table_name
    AND    relam = (SELECT oid FROM pg_catalog.pg_am WHERE amname = 'pg_tde_basic')
    )$$
LANGUAGE SQL;

CREATE FUNCTION pg_tde_rotate_key(new_principal_key_name VARCHAR(255) DEFAULT NULL, new_provider_name VARCHAR(255) DEFAULT NULL, ensure_new_key BOOLEAN DEFAULT TRUE)
RETURNS boolean
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_tde_set_principal_key(principal_key_name VARCHAR(255), provider_name VARCHAR(255), ensure_new_key BOOLEAN DEFAULT FALSE)
RETURNS boolean
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_tde_extension_initialize()
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_tde_principal_key_info()
RETURNS TABLE ( principal_key_name text,
                key_provider_name text,
                key_provider_id integer,
                principal_key_internal_name text,
                principal_key_version integer,
                key_createion_time timestamp with time zone)
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION pg_tde_version() RETURNS TEXT AS 'MODULE_PATHNAME' LANGUAGE C;

-- Access method
CREATE ACCESS METHOD pg_tde_basic TYPE TABLE HANDLER pg_tdeam_basic_handler;
COMMENT ON ACCESS METHOD pg_tde_basic IS 'pg_tde table access method';

DO $$
	BEGIN
		-- Table access method
		CREATE FUNCTION pg_tdeam_handler(internal)
		RETURNS table_am_handler
		AS 'MODULE_PATHNAME'
		LANGUAGE C;

		CREATE ACCESS METHOD pg_tde TYPE TABLE HANDLER pg_tdeam_handler;
		COMMENT ON ACCESS METHOD pg_tde IS 'pg_tde table access method';

		CREATE OR REPLACE FUNCTION pg_tde_ddl_command_start_capture()
		RETURNS event_trigger
		AS 'MODULE_PATHNAME'
		LANGUAGE C;

		CREATE OR REPLACE FUNCTION pg_tde_ddl_command_end_capture()
		RETURNS event_trigger
		AS 'MODULE_PATHNAME'
		LANGUAGE C;

		CREATE EVENT TRIGGER pg_tde_trigger_create_index
		ON ddl_command_start
		EXECUTE FUNCTION pg_tde_ddl_command_start_capture();

		CREATE EVENT TRIGGER pg_tde_trigger_create_index_2
		ON ddl_command_end
		EXECUTE FUNCTION pg_tde_ddl_command_end_capture();
	EXCEPTION WHEN OTHERS THEN
		NULL;
	END;
$$;

-- Per database extension initialization
SELECT pg_tde_extension_initialize();

