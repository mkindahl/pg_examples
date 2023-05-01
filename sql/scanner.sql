CREATE EXTENSION manip;

CREATE TABLE job (job_id serial, body regproc, descr text);

INSERT INTO job(body,descr) VALUES
       ('scan_table', 'Scan table'),
       ('pg_backend_pid', 'Get backend PID');
       
CALL scan_table('job');
