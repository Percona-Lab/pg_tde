/*-------------------------------------------------------------------------
 *
 * pg_tde_tdemap.h
 *	  TDE relation fork manapulation.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_TDE_MAP_H
#define PG_TDE_MAP_H

#include "utils/rel.h"
#include "access/xlog_internal.h"
#include "catalog/pg_tablespace_d.h"
#include "catalog/tde_principal_key.h"
#include "storage/relfilelocator.h"

/* TODO: get rid of that (see. commit message) */
typedef enum InternalKeyRelType
{
	TDE_IKEY_REL_UNKNOWN,
	TDE_IKEY_REL_GLOBAL,
	TDE_IKEY_REL_SMGR,
	TDE_IKEY_REL_BASIC
} InternalKeyRelType;

typedef struct InternalKey
{
	/* 
	 * DO NOT re-arrange fields!
	 * Any changes should be aligned with pg_tde_read/write_one_keydata()
	 */
    uint8			key[INTERNAL_KEY_LEN];
	InternalKeyRelType	rel_type; 

	void*			ctx;
} InternalKey;

#define INTERNAL_KEY_DAT_LEN	offsetof(InternalKey, ctx)

typedef struct RelKeyData
{
    TDEPrincipalKeyId  principal_key_id;
    InternalKey     internal_key;
} RelKeyData;


typedef struct XLogRelKey
{
	RelFileLocator  rlocator;
	RelKeyData      relKey;
	TDEPrincipalKeyInfo pkInfo;
} XLogRelKey;

extern RelKeyData* pg_tde_create_key_map_entry(const RelFileLocator *newrlocator, InternalKeyRelType ktype);
extern void pg_tde_write_key_map_entry(const RelFileLocator *rlocator, RelKeyData *enc_rel_key_data, TDEPrincipalKeyInfo *principal_key_info);
extern void pg_tde_delete_key_map_entry(const RelFileLocator *rlocator);
extern void pg_tde_free_key_map_entry(const RelFileLocator *rlocator, off_t offset);

extern RelKeyData *GetRelationKey(RelFileLocator rel, bool no_map_is_ok);

extern void pg_tde_delete_tde_files(Oid dbOid, Oid spcOid);

extern TDEPrincipalKeyInfo *pg_tde_get_principal_key_info(Oid dbOid, Oid spcOid);
extern bool pg_tde_save_principal_key(TDEPrincipalKeyInfo *principal_key_info);
extern bool pg_tde_perform_rotate_key(TDEPrincipalKey *principal_key, TDEPrincipalKey *new_principal_key);
extern bool pg_tde_write_map_keydata_files(off_t map_size, char *m_file_data, off_t keydata_size, char *k_file_data);
extern RelKeyData* tde_create_rel_key(Oid rel_id, InternalKey *key, TDEPrincipalKeyInfo *principal_key_info);
extern RelKeyData *tde_encrypt_rel_key(TDEPrincipalKey *principal_key, RelKeyData *rel_key_data, const RelFileLocator *rlocator);
extern RelKeyData *tde_decrypt_rel_key(TDEPrincipalKey *principal_key, RelKeyData *enc_rel_key_data, const RelFileLocator *rlocator);
extern RelKeyData *pg_tde_get_key_from_file(const RelFileLocator *rlocator, bool no_map_ok);
extern bool pg_tde_move_rel_key(const RelFileLocator *newrlocator, const RelFileLocator *oldrlocator);

extern void pg_tde_set_db_file_paths(Oid dbOid, Oid spcOid, char *map_path, char *keydata_path);

const char * tde_sprint_key(InternalKey *k);

extern RelKeyData *pg_tde_put_key_into_cache(Oid rel_id, RelKeyData *key);

#endif /*PG_TDE_MAP_H*/
