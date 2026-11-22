INSERT INTO trinity_string (entry, content_default)
SELECT 1029, 'Account name must be an email address containing @.'
WHERE NOT EXISTS (SELECT 1 FROM trinity_string WHERE entry = 1029);
