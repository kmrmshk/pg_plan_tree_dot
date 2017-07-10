SET client_min_messages TO 'warning';

CREATE EXTENSION IF NOT EXISTS pg_plan_tree_dot;

DROP TABLE IF EXISTS employee;

CREATE TABLE employee (
       ID         int PRIMARY KEY,
       name       varchar(10),
       salary     real,
       start_date date,
       city       varchar(10),
       region     char(1));

INSERT INTO employee VALUES (1,  'Jason', 40420,  '1994/02/01', 'New York', 'W');
INSERT INTO employee VALUES (2,  'Robert',14420,  '1995/01/02', 'Vancouver','N');
INSERT INTO employee VALUES (3,  'Celia', 24020,  '1996/12/03', 'Toronto',  'W');
INSERT INTO employee VALUES (4,  'Linda', 40620,  '1987/11/04', 'New York', 'N');
INSERT INTO employee VALUES (5,  'David', 80026,  '1998/10/05', 'Vancouver','W');
INSERT INTO employee VALUES (6,  'James', 70060,  '1999/09/06', 'Toronto',  'N');
INSERT INTO employee VALUES (7,  'Alison',90620,  '2000/08/07', 'New York', 'W');
INSERT INTO employee VALUES (8,  'Chris', 26020,  '2001/07/08', 'Vancouver','N');
INSERT INTO employee VALUES (9,  'Mary',  60020,  '2002/06/09', 'Toronto',  'W');

-- test-01-1
SELECT generate_plan_tree_dot('SELECT region FROM employee GROUP BY region;', 'test-01-1.dot');

-- test-01-2
SELECT generate_plan_tree_dot('SELECT region FROM employee ORDER BY ID;',     'test-01-2.dot');

DROP TABLE employee;
