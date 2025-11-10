create table my_sample(ident int, primary key (ident));
create index my_sample_ident on my_sample(ident);

\d my_sample

insert into my_sample
select ident from generate_series(1,20) ident order by random();

select * from sorted('my_sample', 'my_sample_pkey') t(ident int);
select * from sorted('my_sample', 'my_sample_ident') t(ident int);

