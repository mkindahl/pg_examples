CREATE EXTENSION aggs;

SELECT attrelid::regclass,
       my_array_agg(attname) OVER (ORDER BY n ROWS BETWEEN CURRENT ROW AND 1 FOLLOWING)
    FROM pg_attribute
    WHERE attnum > 0 AND attrelid = 'pg_tablespace'::regclass LIMIT 10;

WITH w AS                                            
(SELECT
  session_id,
  event_name,
  event_time,
  match_recognize(events_small, '{HOMEPAGE,ADD_TO_CART,CLICK,BOOKING,SEARCH}')
    OVER (PARTITION BY session_id
          ORDER BY event_time
          RANGE BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
          ) AS a  FROM events_small)
SELECT session_id, event_name FROM w where a;
