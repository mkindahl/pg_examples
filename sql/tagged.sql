CREATE EXTENSION tagged;

CREATE TABLE jobs(job_id serial, device_id int);

SELECT 'foo'::tagged;

CALL builder('jobs', ARRAY[hash_part('job_id', 3), range_part('device_id', '1 day'::interval)]);
CALL builder('jobs', hash_part('job_id', 3));
CALL builder('jobs', range_part('device_id', '1 day'::interval));
CALL builder('jobs', range_part('unknown_id', '1 day'::interval));
