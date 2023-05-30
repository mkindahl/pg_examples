# Using tagged types for builders

If you want to have a more flexible interface, you can use builders to
construct different objects that can then do the actual job, but since
PostgreSQL cannot support polymorphic objects, you need to cheat.

The canonical example is an array, which has to contain objects of the
same type.

This example solves the problem by defining an array of internal
objects, and have a set of functions (`hash_part` and `range_part`) to
create tagged types.



