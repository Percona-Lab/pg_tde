/*-------------------------------------------------------------------------
 *
 * tde_keyring.c
 *      Deals with the tde keyring configuration
 *      routines.
 *
 * IDENTIFICATION
 *    contrib/pg_tde/src/catalog/tde_keyring.c
 *
 *-------------------------------------------------------------------------
 */

#include "catalog/tde_keyring.h"
#include "catalog/tde_principal_key.h"
#include "access/skey.h"
#include "access/relscan.h"
#include "access/relation.h"
#include "catalog/namespace.h"
#include "utils/lsyscache.h"
#include "access/heapam.h"
#include "utils/snapmgr.h"
#include "utils/fmgroids.h"
#include "common/pg_tde_utils.h"
#include "common/pg_tde_shmem.h"
#include "executor/spi.h"
#include "miscadmin.h"
#include "unistd.h"
#include "utils/builtins.h"
#include "pg_tde.h"

PG_FUNCTION_INFO_V1(pg_tde_add_key_provider_internal);
Datum pg_tde_add_key_provider_internal(PG_FUNCTION_ARGS);


#define PG_TDE_KEYRING_FILENAME "pg_tde_keyrings"
/*
 * These token must be exactly same as defined in
 * pg_tde_add_key_provider_vault_v2 SQL interface
 */
#define VAULTV2_KEYRING_TOKEN_KEY "token"
#define VAULTV2_KEYRING_URL_KEY "url"
#define VAULTV2_KEYRING_MOUNT_PATH_KEY "mountPath"
#define VAULTV2_KEYRING_CA_PATH_KEY "caPath"

/*
 * These token must be exactly same as defined in
 * pg_tde_add_key_provider_file SQL interface
 */
#define FILE_KEYRING_PATH_KEY "path"
#define FILE_KEYRING_TYPE_KEY "type"

typedef enum ProviderScanType
{
	PROVIDER_SCAN_BY_NAME,
	PROVIDER_SCAN_BY_ID,
	PROVIDER_SCAN_BY_TYPE,
	PROVIDER_SCAN_ALL
} ProviderScanType;

static List *scan_key_provider_file(ProviderScanType scanType, void *scanKey);

static FileKeyring *load_file_keyring_provider_options(Datum keyring_options);
static GenericKeyring *load_keyring_provider_options(ProviderType provider_type, Datum keyring_options);
static VaultV2Keyring *load_vaultV2_keyring_provider_options(Datum keyring_options);
static void debug_print_kerying(GenericKeyring *keyring);
static char *get_keyring_infofile_path(char *resPath, Oid dbOid, Oid spcOid);
static uint32 save_key_provider(KeyringProvideRecord *provider);
static void key_provider_startup_cleanup(int tde_tbl_count, void *arg);

static Size initialize_shared_state(void *start_address);
static Size required_shared_mem_size(void);

typedef struct TdeKeyProviderInfoSharedState
{
	LWLock *Locks;
} TdeKeyProviderInfoSharedState;

TdeKeyProviderInfoSharedState*	sharedPrincipalKeyState = NULL; /* Lives in shared state */

static const TDEShmemSetupRoutine key_provider_info_shmem_routine = {
	.init_shared_state = initialize_shared_state,
	.init_dsa_area_objects = NULL,
	.required_shared_mem_size = required_shared_mem_size,
	.shmem_kill = NULL
	};

static Size
required_shared_mem_size(void)
{
	return MAXALIGN(sizeof(TdeKeyProviderInfoSharedState));
}

static Size
initialize_shared_state(void *start_address)
{
	sharedPrincipalKeyState = (TdeKeyProviderInfoSharedState *)start_address;
	sharedPrincipalKeyState->Locks = GetLWLocks();
	return sizeof(TdeKeyProviderInfoSharedState);
}

static inline LWLock *
tde_provider_info_lock(void)
{
	Assert(sharedPrincipalKeyState);
	return &sharedPrincipalKeyState->Locks[TDE_LWLOCK_PI_FILES];
}

void InitializeKeyProviderInfo(void)
{
	ereport(LOG, (errmsg("initializing TDE key provider info")));
	RegisterShmemRequest(&key_provider_info_shmem_routine);
	on_ext_install(key_provider_startup_cleanup, NULL);
}
static void
key_provider_startup_cleanup(int tde_tbl_count, void *arg)
{

	if (tde_tbl_count > 0)
	{
		ereport(WARNING,
				(errmsg("failed to perform initialization. database already has %d TDE tables", tde_tbl_count)));
		return;
	}
	cleanup_key_provider_info(MyDatabaseId, MyDatabaseTableSpace);

	/* TODO: XLog the key cleanup */
	// XLogPrincipalKeyCleanup xlrec;
	// xlrec.databaseId = MyDatabaseId;
	// xlrec.tablespaceId = MyDatabaseTableSpace;
	// XLogBeginInsert();
	// XLogRegisterData((char *)&xlrec, sizeof(TDEPrincipalKeyInfo));
	// XLogInsert(RM_TDERMGR_ID, XLOG_TDE_CLEAN_PRINCIPAL_KEY);
}

ProviderType
get_keyring_provider_from_typename(char *provider_type)
{
	if (provider_type == NULL)
		return UNKNOWN_KEY_PROVIDER;

	if (strcmp(FILE_KEYRING_TYPE, provider_type) == 0)
		return FILE_KEY_PROVIDER;
	if (strcmp(VAULTV2_KEYRING_TYPE, provider_type) == 0)
		return VAULT_V2_KEY_PROVIDER;
	return UNKNOWN_KEY_PROVIDER;
}

static GenericKeyring *
load_keyring_provider_from_record(KeyringProvideRecord* provider)
{
	Datum option_datum;
	GenericKeyring *keyring = NULL;

	option_datum = CStringGetTextDatum(provider->options);

	keyring = load_keyring_provider_options(provider->provider_type, option_datum);
	if (keyring)
	{
		keyring->key_id = provider->provider_id;
		strncpy(keyring->provider_name, provider->provider_name, sizeof(keyring->provider_name));
		keyring->type = provider->provider_type;
		debug_print_kerying(keyring);
	}
	return keyring;
}

List *
GetAllKeyringProviders(void)
{
	return scan_key_provider_file(PROVIDER_SCAN_ALL, NULL);
}

GenericKeyring *
GetKeyProviderByName(const char *provider_name)
{
	GenericKeyring *keyring = NULL;
	List *providers = scan_key_provider_file(PROVIDER_SCAN_BY_NAME, (void*)provider_name);
	if (providers != NIL)
	{
		keyring = (GenericKeyring *)linitial(providers);
		list_free(providers);
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("Key provider \"%s\" does not exists", provider_name),
				 errhint("Use pg_tde_add_key_provider interface to create the key provider")));
	}
	return keyring;
}

GenericKeyring *
GetKeyProviderByID(int provider_id)
{
	GenericKeyring *keyring = NULL;
	List *providers = scan_key_provider_file(PROVIDER_SCAN_BY_ID, &provider_id);
	if (providers != NIL)
	{
		keyring = (GenericKeyring *)linitial(providers);
		list_free(providers);
	}
	return keyring;
}

static GenericKeyring *
load_keyring_provider_options(ProviderType provider_type, Datum keyring_options)
{
	switch (provider_type)
	{
	case FILE_KEY_PROVIDER:
		return (GenericKeyring *)load_file_keyring_provider_options(keyring_options);
		break;
	case VAULT_V2_KEY_PROVIDER:
		return (GenericKeyring *)load_vaultV2_keyring_provider_options(keyring_options);
		break;
	default:
		break;
	}
	return NULL;
}

static FileKeyring *
load_file_keyring_provider_options(Datum keyring_options)
{
	const char* file_path = extract_json_option_value(keyring_options, FILE_KEYRING_PATH_KEY);
	FileKeyring *file_keyring = palloc0(sizeof(FileKeyring));
	
	if(file_path == NULL)
	{
		ereport(DEBUG2,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("file path is missing in the keyring options")));
		return NULL;
	}

	file_keyring->keyring.type = FILE_KEY_PROVIDER;
	strncpy(file_keyring->file_name, file_path, sizeof(file_keyring->file_name));
	return file_keyring;
}

static VaultV2Keyring *
load_vaultV2_keyring_provider_options(Datum keyring_options)
{
	VaultV2Keyring *vaultV2_keyring = palloc0(sizeof(VaultV2Keyring));
	const char* token = extract_json_option_value(keyring_options, VAULTV2_KEYRING_TOKEN_KEY);
	const char* url = extract_json_option_value(keyring_options, VAULTV2_KEYRING_URL_KEY);
	const char* mount_path = extract_json_option_value(keyring_options, VAULTV2_KEYRING_MOUNT_PATH_KEY);
	const char* ca_path = extract_json_option_value(keyring_options, VAULTV2_KEYRING_CA_PATH_KEY);

	if(token == NULL || url == NULL || mount_path == NULL)
	{
		/* TODO: report error */
		return NULL;
	}

	vaultV2_keyring->keyring.type = VAULT_V2_KEY_PROVIDER;
	strncpy(vaultV2_keyring->vault_token, token, sizeof(vaultV2_keyring->vault_token));
	strncpy(vaultV2_keyring->vault_url, url, sizeof(vaultV2_keyring->vault_url));
	strncpy(vaultV2_keyring->vault_mount_path, mount_path, sizeof(vaultV2_keyring->vault_mount_path));
	strncpy(vaultV2_keyring->vault_ca_path, ca_path ? ca_path : "", sizeof(vaultV2_keyring->vault_ca_path));
	return vaultV2_keyring;
}

static void
debug_print_kerying(GenericKeyring *keyring)
{
	int debug_level = DEBUG2;
	elog(debug_level, "Keyring type: %d", keyring->type);
	elog(debug_level, "Keyring name: %s", keyring->provider_name);
	elog(debug_level, "Keyring id: %d", keyring->key_id);
	switch (keyring->type)
	{
	case FILE_KEY_PROVIDER:
		elog(debug_level, "File Keyring Path: %s", ((FileKeyring *)keyring)->file_name);
		break;
	case VAULT_V2_KEY_PROVIDER:
		elog(debug_level, "Vault Keyring Token: %s", ((VaultV2Keyring *)keyring)->vault_token);
		elog(debug_level, "Vault Keyring URL: %s", ((VaultV2Keyring *)keyring)->vault_url);
		elog(debug_level, "Vault Keyring Mount Path: %s", ((VaultV2Keyring *)keyring)->vault_mount_path);
		elog(debug_level, "Vault Keyring CA Path: %s", ((VaultV2Keyring *)keyring)->vault_ca_path);
		break;
	case UNKNOWN_KEY_PROVIDER:
		elog(debug_level, "Unknown Keyring ");
		break;
	}
}

/*
 * Fetch the next key provider from the file and update the curr_pos
*/
static bool
fetch_next_key_provider(int fd, off_t* curr_pos, KeyringProvideRecord *provider)
{
	off_t bytes_read = 0;

	Assert(provider != NULL);
	Assert(fd >= 0);

	bytes_read = pg_pread(fd, provider, sizeof(KeyringProvideRecord), *curr_pos);
	*curr_pos += bytes_read;

	if (bytes_read == 0)
		return false;
	if (bytes_read != sizeof(KeyringProvideRecord))
	{
		close(fd);
		/* Corrupt file */
		ereport(ERROR,
				(errcode_for_file_access(),
					errmsg("key provider info file is corrupted: %m"),
					errdetail("invalid key provider record size %lld expected %lu", bytes_read, sizeof(KeyringProvideRecord) )));
	}
	return true;
}

/*
* Save the key provider info to the file
*/
static uint32
save_key_provider(KeyringProvideRecord *provider)
{
	off_t bytes_written = 0;
	off_t curr_pos = 0;
	int fd;
	int max_provider_id = 0;
	char kp_info_path[MAXPGPATH] = {0};
	KeyringProvideRecord existing_provider;

	Assert(provider != NULL);

	get_keyring_infofile_path(kp_info_path, MyDatabaseId, MyDatabaseTableSpace);

	LWLockAcquire(tde_provider_info_lock(), LW_EXCLUSIVE);

	fd = BasicOpenFile(kp_info_path, O_CREAT | O_RDWR | PG_BINARY);
	if (fd < 0)
	{
		LWLockRelease(tde_provider_info_lock());
		ereport(ERROR,
			(errcode_for_file_access(),
				errmsg("could not open tde file \"%s\": %m", kp_info_path)));
	}

	/* we also need to verify the name conflict and generate the next provider ID */
	while (fetch_next_key_provider(fd, &curr_pos, &existing_provider))
	{
		if (strcmp(existing_provider.provider_name, provider->provider_name) == 0)
		{
			close(fd);
			LWLockRelease(tde_provider_info_lock());
			ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
					errmsg("key provider \"%s\" already exists", provider->provider_name)));
		}
		if (max_provider_id < existing_provider.provider_id)
			max_provider_id = existing_provider.provider_id;
	}
	provider->provider_id = max_provider_id + 1;
	/*
	 * All good, Just add a new provider
	 * Write key to the end of file
	 */
	curr_pos = lseek(fd, 0, SEEK_END);
	bytes_written = pg_pwrite(fd, provider, sizeof(KeyringProvideRecord), curr_pos);
	if (bytes_written != sizeof(KeyringProvideRecord))
	{
		close(fd);
		LWLockRelease(tde_provider_info_lock());
		ereport(ERROR,
			(errcode_for_file_access(),
				errmsg("key provider info file \"%s\" can't be written: %m",
						kp_info_path)));
	}
	if (pg_fsync(fd) != 0)
	{
		close(fd);
		LWLockRelease(tde_provider_info_lock());
		ereport(ERROR,
			(errcode_for_file_access(),
				errmsg("could not fsync file \"%s\": %m",
						kp_info_path)));
	}
	close(fd);
	LWLockRelease(tde_provider_info_lock());
	return provider->provider_id;
}

/*
 * Scan the key provider info file and can also apply filter based on scanType
 */
static List *
scan_key_provider_file(ProviderScanType scanType, void* scanKey)
{
	off_t curr_pos = 0;
	int fd;
	char kp_info_path[MAXPGPATH] = {0};
	KeyringProvideRecord provider;
	List *providers_list = NIL;

	if (scanType != PROVIDER_SCAN_ALL)
		Assert(scanKey != NULL);

	get_keyring_infofile_path(kp_info_path, MyDatabaseId, MyDatabaseTableSpace);

	LWLockAcquire(tde_provider_info_lock(), LW_SHARED);

	fd = BasicOpenFile(kp_info_path, PG_BINARY);
	if (fd < 0)
	{
		LWLockRelease(tde_provider_info_lock());
		ereport(DEBUG2,
			(errcode_for_file_access(),
				errmsg("could not open tde file \"%s\": %m", kp_info_path)));
		return NIL;
	}
	/* we also need to verify the name conflixt and generate the next provider ID */
	while (fetch_next_key_provider(fd, &curr_pos, &provider))
	{
		bool match = false;
		ereport(DEBUG2,
			(errmsg("read key provider ID=%d %s", provider.provider_id, provider.provider_name)));

		if (scanType == PROVIDER_SCAN_BY_NAME)
		{
			if (strcasecmp(provider.provider_name, (char*)scanKey) == 0)
				match = true;
		}
		else if (scanType == PROVIDER_SCAN_BY_ID)
		{
			if (provider.provider_id == *(int *)scanKey)
				match = true;
		}
		else if (scanType == PROVIDER_SCAN_BY_TYPE)
		{
			if (provider.provider_type == *(ProviderType*)scanKey)
				match = true;
		}
		else if (scanType == PROVIDER_SCAN_ALL)
			match = true;

		if (match)
		{
			GenericKeyring *keyring = load_keyring_provider_from_record(&provider);
			if (keyring)
			{
				providers_list = lappend(providers_list, keyring);
			}
		}
	}
	close(fd);
	LWLockRelease(tde_provider_info_lock());
	return providers_list;
}

void
cleanup_key_provider_info(Oid databaseId, Oid tablespaceId)
{
	/* Remove the key provider info fileß */
	char kp_info_path[MAXPGPATH] = {0};

	get_keyring_infofile_path(kp_info_path, MyDatabaseId, MyDatabaseTableSpace);
	PathNameDeleteTemporaryFile(kp_info_path, false);
}

static char*
get_keyring_infofile_path(char* resPath, Oid dbOid, Oid spcOid)
{
	char *db_path = pg_tde_get_tde_file_dir(dbOid, spcOid);
	Assert(db_path != NULL);
	join_path_components(resPath, db_path, PG_TDE_KEYRING_FILENAME);
	pfree(db_path);
	return resPath;
}

Datum
pg_tde_add_key_provider_internal(PG_FUNCTION_ARGS)
{
	char *provider_type = text_to_cstring(PG_GETARG_TEXT_PP(0));
	char *provider_name = text_to_cstring(PG_GETARG_TEXT_PP(1));
	char *options = text_to_cstring(PG_GETARG_TEXT_PP(2));
	KeyringProvideRecord provider;

	strncpy(provider.options, options, sizeof(provider.options));
	strncpy(provider.provider_name, provider_name, sizeof(provider.provider_name));
	provider.provider_type = get_keyring_provider_from_typename(provider_type);
	save_key_provider(&provider);
	PG_RETURN_INT32(provider.provider_id);
}
