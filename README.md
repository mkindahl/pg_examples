# PostgreSQL Examples

Repository with PostgreSQL example extensions mostly used for
testing. It contains experiments on permission handling and
extensions, some tests on how to use internal functions and how to
accomplish different results, and example solutions for how to solve
particular problems.

## Building and installing

To build, install, and run tests

```bash
make
[sudo] make install
make installcheck
```

## Documentation

* [Extension `reporter`](docs/reporter.md) is an example of how to run
  background worker regularly.
* [Extension `simple`](docs/simple.md) demonstrates a problem with
  permissions and extensions that was brought up in `pgsql-bugs`

