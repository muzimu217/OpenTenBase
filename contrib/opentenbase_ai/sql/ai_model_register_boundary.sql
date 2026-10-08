-- Boundary tests for duplicate model registration (self-describing error)
CREATE EXTENSION IF NOT EXISTS http;
CREATE EXTENSION IF NOT EXISTS opentenbase_ai;

-- Registering a new model succeeds
SELECT ai.add_model(
    'boundary_model',
    ARRAY[
        ROW('Content-Type', 'application/json')::http_header
    ],
    'https://example.invalid/post',
    '{"model": "boundary"}'::jsonb,
    'test-provider',
    'POST',
    'application/json',
    'SELECT %s::text'
) AS first_registration;

-- Registering the same model name again must fail with a self-describing
-- error that names the model and preserves the unique_violation SQLSTATE
DO $$
DECLARE
    v_sqlstate text;
    v_message  text;
BEGIN
    BEGIN
        PERFORM ai.add_model(
            'boundary_model',
            ARRAY[
                ROW('Content-Type', 'application/json')::http_header
            ],
            'https://example.invalid/post',
            '{"model": "boundary"}'::jsonb,
            'test-provider',
            'POST',
            'application/json',
            'SELECT %s::text'
        );
        RAISE EXCEPTION 'duplicate registration unexpectedly succeeded';
    EXCEPTION
        WHEN unique_violation THEN
            GET STACKED DIAGNOSTICS v_sqlstate = RETURNED_SQLSTATE,
                                    v_message = MESSAGE_TEXT;
            RAISE NOTICE 'caught %: %', v_sqlstate, v_message;
    END;
END $$;

-- After deleting the model, the name can be registered again
SELECT ai.delete_model('boundary_model') AS delete_before_reuse;
SELECT ai.add_model(
    'boundary_model',
    ARRAY[
        ROW('Content-Type', 'application/json')::http_header
    ],
    'https://example.invalid/post',
    '{"model": "boundary"}'::jsonb,
    'test-provider',
    'POST',
    'application/json',
    'SELECT %s::text'
) AS re_registration;

-- Cleanup
SELECT ai.delete_model('boundary_model') AS cleanup;
