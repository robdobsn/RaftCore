#!/bin/bash
set -e

echo "Building and running unit tests using Docker..."
cd "$(dirname "$0")"

# Build with detailed progress
docker-compose build --progress=plain

# Run tests and capture exit code
docker-compose run --rm unit-tests
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo "ERROR: Tests failed with exit code $EXIT_CODE"
    exit $EXIT_CODE
else
    echo "All tests completed successfully!"
fi 