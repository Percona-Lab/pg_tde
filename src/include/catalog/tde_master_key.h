/*-------------------------------------------------------------------------
 *
 * tde_master_key.h
 *	  TDE master key handling
 *
 * src/include/catalog/tde_master_key.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_TDE_MASTER_KEY_H
#define PG_TDE_MASTER_KEY_H


#include "postgres.h"
#include "catalog/tde_keyring.h"
#include "keyring/keyring_api.h"
#include "nodes/pg_list.h"

#define MASTER_KEY_NAME_LEN TDE_KEY_NAME_LEN

typedef struct TDEMasterKeyId
{
	uint32	version;
	char	name[MASTER_KEY_NAME_LEN];
} TDEMasterKeyId;

typedef struct TDEMasterKey
{
	TDEMasterKeyId keyId;
	Oid databaseId;
	Oid keyringId;
	unsigned char keyData[MAX_KEY_DATA_SIZE];
	uint32 keyLength;
} TDEMasterKey;

typedef struct TDEMasterKeyInfo
{
	Oid keyringId;
	Oid databaseId;
	Oid tablespaceId;
	Oid userId;
	struct timeval creationTime;
	TDEMasterKeyId keyId;
} TDEMasterKeyInfo;

extern void InitializeMasterKeyInfo(void);
extern TDEMasterKey* GetMasterKey(void);
extern TDEMasterKey* SetMasterKey(const char* key_name, const char* provider_name);
extern Oid GetMasterKeyProviderId(void);
extern void save_master_key_info(TDEMasterKeyInfo *masterKeyInfo);

#endif /*PG_TDE_MASTER_KEY_H*/
