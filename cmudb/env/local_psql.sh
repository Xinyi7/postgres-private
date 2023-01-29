#!/bin/bash

set -euxo pipefail

BUILD_DIR="$(pwd)/build/postgres/"
BIN_DIR="$(pwd)/build/postgres/bin/"
POSTGRES_USER="noisepage_user"
POSTGRES_PASSWORD="noisepage_pass"
POSTGRES_DB="noisepage_db"
POSTGRES_PORT=15721

if ! PGPASSWORD=${POSTGRES_PASSWORD} "${BIN_DIR}"/psql -U ${POSTGRES_USER} -d ${POSTGRES_DB} -p ${POSTGRES_PORT} -c "SELECT 1" >/dev/null; then
  "${BIN_DIR}"/psql -c "create user ${POSTGRES_USER} with login password '${POSTGRES_PASSWORD}'" postgres -p ${POSTGRES_PORT}
  "${BIN_DIR}"/psql -c "create database ${POSTGRES_DB} with owner = '${POSTGRES_USER}'" postgres -p ${POSTGRES_PORT}
  "${BIN_DIR}"/psql -c "grant pg_monitor to ${POSTGRES_USER}" postgres -p ${POSTGRES_PORT}
  "${BIN_DIR}"/psql -c "alter user ${POSTGRES_USER} with superuser" postgres -p ${POSTGRES_PORT}

  PGPASSWORD=${POSTGRES_PASSWORD} "${BIN_DIR}"/psql -U ${POSTGRES_USER} -d ${POSTGRES_DB} -p ${POSTGRES_PORT} --echo-all -f ./cmudb/extensions/db721_fdw/chicken_farm_schema.sql
fi
PGPASSWORD=${POSTGRES_PASSWORD} "${BIN_DIR}"/psql -U ${POSTGRES_USER} -d ${POSTGRES_DB} -p ${POSTGRES_PORT}
