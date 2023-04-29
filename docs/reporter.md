# Run background worker regularly

This is to test that we can create a background worker that restarts
on a regular basis. It can be used for sending telemetry reports, for
example.

The reporter uses the fact that you can set a restart time for the
background worker and if it exits with a non-zero status, it will be
started again at a later time.

This it is already used for the logical replication worker, so we are
just doing the same here.

## Configuration

| **Configuration**          | **Description**                                |
|:---------------------------|:-----------------------------------------------|
| `reporter.database`        | Database to connect to and collect information |
| `reporter.report_interval` | Report interval in seconds                     |


## Files

| **File**            | **Description**        |
|:--------------------|:-----------------------|
| `reporter--0.1.sql` | Install script         |
| `reporter.control`  | Extension control file |
| `reporter.c`        | C implementation file  |

