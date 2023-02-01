CREATE TABLE IF NOT EXISTS farm
(
    farm_name       varchar,
--     sexes         varchar[],
    min_age_weeks   real,
    max_age_weeks   real
);

CREATE TABLE IF NOT EXISTS chicken
(
    identifier      integer,
    farm_name       varchar,
    weight_model    varchar,
    sex             varchar,
    age_weeks       real,
    weight_g        real,
    notes           varchar
);

\COPY farm FROM './data-farms.csv' CSV HEADER;
\COPY chicken FROM './data-chickens.csv' CSV HEADER;

CREATE EXTENSION IF NOT EXISTS db721_fdw;
CREATE SERVER IF NOT EXISTS db721_server FOREIGN DATA WRAPPER db721_fdw;
CREATE FOREIGN TABLE IF NOT EXISTS db721_farm
(
    farm_name       varchar,
--     sexes           varchar[],
    min_age_weeks   real,
    max_age_weeks   real
) SERVER db721_server OPTIONS
(
    filename '/home/kapi/git/postgres/data-farms.db721',
    tablename 'Farm'
);
CREATE FOREIGN TABLE IF NOT EXISTS db721_chicken (
    identifier      integer,
    farm_name       varchar,
    weight_model    varchar,
    sex             varchar,
    age_weeks       real,
    weight_g        real,
    notes           varchar
) SERVER db721_server OPTIONS
(
    filename '/home/kapi/git/postgres/data-chickens.db721',
    tablename 'Chicken'
);
