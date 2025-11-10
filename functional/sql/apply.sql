create function my_test(text, integer) returns text
as $$
begin
  return format('%s-%s', $1, $2::text);
end
$$ language plpgsql;

create function apply(name, text, integer) returns text as 'functional' language c;

select apply('my_test', 'foo', i) from generate_series(1,10) i;
