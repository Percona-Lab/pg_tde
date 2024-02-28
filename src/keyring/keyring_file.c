/*-------------------------------------------------------------------------
 *
 * keyring_file.c
 *      Implements the file provider keyring
 *      routines.
 *
 * IDENTIFICATION
 *    contrib/pg_tde/src/keyring/keyring_file.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "keyring/keyring_file.h"
#include "keyring/keyring_config.h"
#include "catalog/tde_keyring.h"
#include "keyring/keyring_api.h"
#include "storage/fd.h"
#include "utils/wait_event.h"
#include <stdio.h>
#include <unistd.h>

static keyInfo* get_key_by_name(GenericKeyring* keyring, const char* key_name, bool throw_error, KeyringReturnCodes *return_code);
static KeyringReturnCodes set_key_by_name(GenericKeyring* keyring, keyInfo *key, bool throw_error);

const TDEKeyringRoutine keyringFileRoutine = {
	.keyring_get_key = get_key_by_name,
	.keyring_store_key = set_key_by_name
};

bool
InstallFileKeyring(void)
{
	return RegisterKeyProvider(&keyringFileRoutine, FILE_KEY_PROVIDER);
}


static keyInfo*
get_key_by_name(GenericKeyring* keyring, const char* key_name, bool throw_error, KeyringReturnCodes *return_code)
{
	keyInfo* key = NULL;
	File file = -1;
	FileKeyring* file_keyring = (FileKeyring*)keyring;

	file = PathNameOpenFile(file_keyring->file_name, O_CREAT | O_RDWR | PG_BINARY);
	if (file < 0)
	{
		*return_code = KEYRING_CODE_RESOURCE_NOT_ACCESSABLE;
			ereport(throw_error ? ERROR : NOTICE,
					(errmsg("Failed to open keyring file :%s %m", file_keyring->file_name)));
		return NULL;
	}

	key = palloc(sizeof(keyInfo));
	while(true)
	{
		off_t bytes_read = 0;
		bytes_read = FileRead(file, key, sizeof(keyInfo), 0, WAIT_EVENT_DATA_FILE_READ);
		if (bytes_read == 0 )
		{
			pfree(key);
			return NULL;
		}
		if (bytes_read != sizeof(keyInfo))
		{
			pfree(key);
			/* Corrupt file */
			*return_code = KEYRING_CODE_DATA_CORRUPTED;
			ereport(throw_error?ERROR:WARNING,
				(errcode_for_file_access(),
					errmsg("keyring file \"%s\" is corrupted: %m",
						file_keyring->file_name)));
			return NULL;
		}
		if (strncasecmp(key->name.name, key_name, sizeof(key->name.name)) == 0)
		{
		    FileClose(file);
			return key;
		}
	}
	*return_code = KEYRING_CODE_SUCCESS;
	FileClose(file);
	pfree(key);
    return NULL;
}

static KeyringReturnCodes
set_key_by_name(GenericKeyring* keyring, keyInfo *key, bool throw_error)
{
    off_t bytes_written = 0;
	File file;
	FileKeyring* file_keyring = (FileKeyring*)keyring;
	keyInfo *existing_key;
	KeyringReturnCodes return_code = KEYRING_CODE_SUCCESS;

	Assert(key != NULL);
	/* See if the key with same name already exists */
	existing_key = get_key_by_name(keyring, key->name.name, false, &return_code);
	if (existing_key)
	{
		pfree(existing_key);
		ereport(throw_error ? ERROR : WARNING,
				(errmsg("Key with name %s already exists in keyring", key->name.name)));
		return KEYRING_CODE_INVALID_OPERATION;
	}

    file = PathNameOpenFile(file_keyring->file_name, O_CREAT | O_RDWR | PG_BINARY);
    if (file < 0)
    {
		ereport(throw_error?ERROR:WARNING,
			(errcode_for_file_access(),
			errmsg("Failed to open keyring file %s :%m", file_keyring->file_name)));
        return KEYRING_CODE_RESOURCE_NOT_ACCESSABLE;
    }
	/* Write key to the end of file */
	lseek(file, 0, SEEK_END);
	bytes_written = FileWrite(file, key, sizeof(keyInfo), 0, WAIT_EVENT_DATA_FILE_WRITE);
    if (bytes_written != sizeof(keyInfo))
    {
        FileClose(file);
        ereport(throw_error?ERROR:WARNING,
                 (errcode_for_file_access(),
                  errmsg("keyring file \"%s\" can't be written: %m",
                     file_keyring->file_name)));
        return KEYRING_CODE_RESOURCE_NOT_ACCESSABLE;
    }
    FileClose(file);
	return KEYRING_CODE_SUCCESS;
}
