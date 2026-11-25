-- Match the Developer V2 client data revision. Keep newer local revisions.
UPDATE version SET cache_id = 20261110 WHERE cache_id < 20261110;
UPDATE version SET hotfix_cache_id = 20261110 WHERE hotfix_cache_id < 20261110;
