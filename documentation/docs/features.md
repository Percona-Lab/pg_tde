# Build comparison

We provide `pg_tde` builds for both PostgreSQL Community and [Percona Server for PostgreSQL](https://docs.percona.com/postgresql/latest/postgresql-server.html). The difference between the builds is in the set of included features which in turn depends on the Storage Manager API. While PostgreSQL Community uses the default Storage Manager API, Percona Server for PostgreSQL extends the Storage Manager API enabling to integrate custom storage managers.

## Features

The following table provides the differences between the builds:

| PostgreSQL Community build  | Percona Server for PostgreSQL build <br> |
|----------------------|-------------------------------|
| Table encryption: <br> - data tables, <br> - TOAST tables <br> - temporary tables created during the database operation.<br><br> Metadata of those tables is not encrypted. | Table encryption: <br> - data tables, <br> - TOAST tables <br> - temporary tables created during the database operation.<br> - Index data for encrypted tables<br><br> Metadata of those tables is not encrypted.  |
| Write-Ahead Log (WAL) encryption of data in encrypted tables | Write-Ahead Log (WAL) encryption of data for encrypted and non-encrypted tables  |
| Multi-tenancy support| Multi-tenancy support |
| Table-level granularity |Table-level granularity | 
| Key management via: <br> - HashiCorp Vault; <br> - Local keyfile | Key management via: <br> - HashiCorp Vault; <br> - Local keyfile|
| | Logical replication of encrypted tables | 

## Future releases

The following is planned for future releases of `pg_tde`:

* KMIP integration for key management
* Global principal keys 


[Get started](install.md){.md-button}