create extension pgcrypto;

select setseed(1.0);

create table my_sample (
    ident int not null,
    created_at timestamptz not null,
    value int,
    primary key (ident)
);

-- non-unique index
create index my_sample_idx1 on my_sample (value);

-- unique index for replica identity
create unique index my_sample_idx2 on my_sample (created_at);

\d my_sample
insert into my_sample
select ident,
       '2025-11-11 07:30:00'::timestamptz + 1000 * random() * '1 microsecond'::interval,
       1000 * random()
from generate_series(1, 20) ident order by random() on conflict do nothing;

select * from sorted('my_sample', 'my_sample_pkey') t(ident int, ts timestamptz, value int);
select * from sorted('my_sample', 'my_sample_idx1') t(ident int, ts timestamptz, value int);
select * from sorted('my_sample') t(ident int, ts timestamptz, value int);

alter table my_sample replica identity using index my_sample_idx2;

select * from sorted('my_sample') t(ident int, ts timestamptz, value int);

alter table my_sample replica identity full;

\set ON_ERROR_STOP 0
select * from sorted('my_sample_idx2', 'my_sample') t(ident int, ts timestamptz, value int);
select * from sorted('my_sample', 'my_sample_idx3') t(ident int, ts timestamptz, value int);
select * from sorted('my_sample') t(ident int, ts timestamptz, value int);
\set ON_ERROR_STOP 1

