# Support for functional programming

Different concepts that can be useful in functional programming. Most
are just demonstrations for how to actually implementing a concept.

## Apply a function to a set of arguments

In functional programming languages, `apply` or `eval` is a central
concept. In these, you can apply a function to a set of arguments to
execute it, e.g., to apply the function `+` to two values in Scheme:

```scheme
(apply + '(1 2))
```

This can be done in PostgreSQL as well, but the syntax does not
support passing arbitrary parameters to a function. However, there is
nothing in the internals that prevent this from working, so by
implementing a function `apply` in C that can take a function and a
list of arbitrary arguments, we can make this work.

First, you have to pre-declare the function with the correct
parameters. The first argument is the function to apply, by name.

```sql
create function apply(name, text, integer) returns text as 'functional' language c;
```

You can now create an arbitrary function that takes a `text` and an `integer`:

```sql
create function my_test(text, integer) returns text
as $$
begin
  return format('%s-%s', $1, $2::text);
end
$$ language plpgsql;
```

And then use the `apply` function to execute this function:

```sql
select apply('my_test', 'foo', i) from generate_series(1,10) i;
```

## Tuplesort as a function

This is an experiment of using tuplesort in different ways. Note that
this is the same as using an `ORDER BY` clause for the select, so
there is really no advantage of this. It is just inteded to
demonstrate how to use tuplesort internally.

The function `sorted` can take a table and an optional index and
return the sorted version of this, for example:

```sql
select *
  from sorted('my_sample', 'my_sample_idx1') 
            t(ident int, ts timestamptz, value int);
```

If you do not provide an index, the replica identity will be used, if
one exists that use an index.
