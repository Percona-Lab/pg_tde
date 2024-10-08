# `pg_tde` documentation

`pg_tde` is the extension that brings in [Transparent Data Encryption (TDE)](tde.md) to PostgreSQL and enables users to keep sensitive data safe and secure. It enables users to configure encryption differently for each database, encrypting specific tables in some databases with different encryption keys, while keeping others non encrypted. 

!!! important 

    This is the {{release}} version of the extension and is not meant for production use yet. [Release notes](release-notes/release-notes.md)

## Supported PostgreSQL deployments

 `pg_tde` is provided in database specific builds for:

* [Percona Server for PostgreSQL 17](https://docs.percona.com/postgresql/17/postgresql-server.html) as part of Percona Distribution for PostgreSQL 17. This build includes all capabilities and features of `pg_tde`
* PostgreSQL Community 16.x, PostgreSQL Community 17.0 and Percona Distribution for PostgreSQL 16.0. This build provides limited capabilities. Namely, index level encryption is not supported as it requires the use of a custom storage manager.

[Compare builds](features.md){.md-button}
[Get started](install.md){.md-button}

## Known limitations

* Logical replication is not available as it doesn't work with encrypted tables.
* Keys in the local keyfile are stored unencrypted.
* System tables are currently not encrypted

<i warning>:material-alert: Warning:</i> Note that introducing encryption/decryption affects performance. Our benchmark tests show less than 10% performance overhead for most situations. However, in some specific applications such as those using JSONB operations, performance degradation might be higher.

## Useful links

* [What is Transparent Data Encryption](tde.md)

