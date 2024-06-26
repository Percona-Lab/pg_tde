/*-------------------------------------------------------------------------
 *
 * tde_global_catalog.c
 *	  Global catalog key management
 *
 *
 * IDENTIFICATION
 *	  src/catalog/tde_global_catalog.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#ifdef PERCONA_FORK

#include "storage/shmem.h"
#include "utils/guc.h"

#include "access/pg_tde_tdemap.h"
#include "catalog/tde_global_catalog.h"
#include "catalog/tde_keyring.h"
#include "catalog/tde_principal_key.h"

#include <openssl/rand.h>
#include <openssl/err.h>
#include <sys/time.h>

#define PRINCIPAL_KEY_DEFAULT_NAME	"tde-global-catalog-key"

/* TODO: not sure if we need an option of multiple principal keys for the global catalog */
typedef enum
{
	TDE_GCAT_XLOG_KEY,

	/* must be last */
	TDE_GCAT_KEYS_COUNT
}			GlobalCatalogKeyTypes;

typedef struct EncryptionStateData
{
	GenericKeyring *keyring;
	TDEPrincipalKey principal_keys[TDE_GCAT_KEYS_COUNT];
}			EncryptionStateData;

static EncryptionStateData * EncryptionState = NULL;

/* GUC */
static char *KRingProviderType = NULL;
static char *KRingProviderFilePath = NULL;

static void init_gl_catalog_keys(void);
static void init_keyring(void);
static TDEPrincipalKey * create_principal_key(const char *key_name,
										GenericKeyring * keyring, Oid dbOid, Oid spcOid,
										bool ensure_new_key);

void
TDEGlCatInitGUC(void)
{
	DefineCustomStringVariable("pg_tde.global_keyring_type",
							   "Keyring type for global catalog",
							   NULL,
							   &KRingProviderType,
							   NULL,
							   PGC_POSTMASTER,
							   0,	/* no flags required */
							   NULL,
							   NULL,
							   NULL
		);
	DefineCustomStringVariable("pg_tde.global_keyring_file_path",
							   "Keyring file options for global catalog",
							   NULL,
							   &KRingProviderFilePath,
							   NULL,
							   PGC_POSTMASTER,
							   0,	/* no flags required */
							   NULL,
							   NULL,
							   NULL
		);
}


Size
TDEGlCatEncStateSize(void)
{
	Size		size;

	size = sizeof(EncryptionStateData);
	size = add_size(size, sizeof(KeyringProviders));

	return MAXALIGN(size);
}

void
TDEGlCatShmemInit(void)
{
	bool		foundBuf;
	char	   *allocptr;

	EncryptionState = (EncryptionStateData *)
		ShmemInitStruct("TDE XLog Encryption State",
						TDEGlCatEncStateSize(), &foundBuf);

	allocptr = ((char *) EncryptionState) + MAXALIGN(sizeof(EncryptionStateData));
	EncryptionState->keyring = (GenericKeyring *) allocptr;
	memset(EncryptionState->keyring, 0, sizeof(KeyringProviders));
	memset(EncryptionState->principal_keys, 0, sizeof(TDEPrincipalKey) * TDE_GCAT_KEYS_COUNT);
}

void
TDEGlCatKeyInit(void)
{
	char		db_map_path[MAXPGPATH] = {0};

	init_keyring();

	pg_tde_set_db_file_paths(&GLOBAL_SPACE_RLOCATOR(XLOG_TDE_OID),
							 db_map_path, NULL);
	if (access(db_map_path, F_OK) == -1)
	{
		init_gl_catalog_keys();
	}
	else
	{
		/* put an internal key into the cache */
		GetGlCatInternalKey(XLOG_TDE_OID);
	}
}

TDEPrincipalKey *
TDEGetGlCatKeyFromCache(void)
{
	TDEPrincipalKey *mkey;

	mkey = &EncryptionState->principal_keys[TDE_GCAT_XLOG_KEY];
	if (mkey->keyLength == 0)
		return NULL;

	return mkey;
}

void
TDEPutGlCatKeyInCache(TDEPrincipalKey * mkey)
{
	memcpy(EncryptionState->principal_keys + TDE_GCAT_XLOG_KEY, mkey, sizeof(TDEPrincipalKey));
}

RelKeyData *
GetGlCatInternalKey(Oid obj_id)
{
	return GetRelationKeyWithKeyring(GLOBAL_SPACE_RLOCATOR(obj_id), EncryptionState->keyring);
}

/* 
 * TODO: should be aligned with the rest of the keyring_provider code after its
 * 		 refactoring
 *
 * TODO: add Vault 
 */
static void
init_keyring(void)
{
	EncryptionState->keyring->type = get_keyring_provider_from_typename(KRingProviderType);
	switch (EncryptionState->keyring->type)
	{
		case FILE_KEY_PROVIDER:
			FileKeyring * kring = (FileKeyring *) EncryptionState->keyring;
			strncpy(kring->file_name, KRingProviderFilePath, sizeof(kring->file_name));
			break;
	}
}

/*
 * Keys are created during the cluster start only, so no locks needed here.
 */
static void
init_gl_catalog_keys(void)
{
	InternalKey int_key;
	RelKeyData *rel_key_data;
	RelKeyData *enc_rel_key_data;
	RelFileLocator *rlocator;
	TDEPrincipalKey *mkey;

	mkey = create_principal_key(PRINCIPAL_KEY_DEFAULT_NAME,
							 EncryptionState->keyring,
							 GLOBAL_DATA_TDE_OID, GLOBALTABLESPACE_OID, false);

	memset(&int_key, 0, sizeof(InternalKey));

	/* Create and store an internal key for XLog */
	if (!RAND_bytes(int_key.key, INTERNAL_KEY_LEN))
	{
		ereport(FATAL,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("could not generate internal key for \"WAL\": %s",
						ERR_error_string(ERR_get_error(), NULL))));
	}

	rlocator = &GLOBAL_SPACE_RLOCATOR(XLOG_TDE_OID);
	rel_key_data = tde_create_rel_key(rlocator->relNumber, &int_key, &mkey->keyInfo);
	enc_rel_key_data = tde_encrypt_rel_key(mkey, rel_key_data, rlocator);
	pg_tde_write_key_map_entry(rlocator, enc_rel_key_data, &mkey->keyInfo);

	/* 
	 * TODO: move global catalog internal keys into own cache. This cache should
	 * be in the TopMemmoryContext because of SSL contexts
	 * (see https://github.com/Percona-Lab/pg_tde/pull/214#discussion_r1648998317)
	*/
	pg_tde_put_key_into_map(rlocator->relNumber, rel_key_data);
	TDEPutGlCatKeyInCache(mkey);
}

static TDEPrincipalKey *
create_principal_key(const char *key_name, GenericKeyring * keyring,
				  Oid dbOid, Oid spcOid, bool ensure_new_key)
{
	TDEPrincipalKey *principalKey;
	keyInfo    *keyInfo = NULL;

	principalKey = palloc(sizeof(TDEPrincipalKey));
	principalKey->keyInfo.databaseId = dbOid;
	principalKey->keyInfo.tablespaceId = spcOid;
	principalKey->keyInfo.keyId.version = DEFAULT_PRINCIPAL_KEY_VERSION;
	principalKey->keyInfo.keyringId = keyring->key_id;
	strncpy(principalKey->keyInfo.keyId.name, key_name, TDE_KEY_NAME_LEN);
	gettimeofday(&principalKey->keyInfo.creationTime, NULL);

	keyInfo = load_latest_versioned_key_name(&principalKey->keyInfo, keyring, ensure_new_key);

	if (keyInfo == NULL)
		keyInfo = KeyringGenerateNewKeyAndStore(keyring, principalKey->keyInfo.keyId.versioned_name, INTERNAL_KEY_LEN, false);

	if (keyInfo == NULL)
	{
		ereport(ERROR,
				(errmsg("failed to retrieve principal key")));
	}

	principalKey->keyLength = keyInfo->data.len;
	memcpy(principalKey->keyData, keyInfo->data.data, keyInfo->data.len);

	return principalKey;
}
#endif /* PERCONA_FORK */
