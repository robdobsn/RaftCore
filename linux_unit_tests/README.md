# Linux Unit Tests with Docker

This directory contains unit tests for the RaftCore project that can be run in a Docker container, eliminating the need for setting up a Linux environment or cross-compiler on your local machine.

## Prerequisites

- Docker
- Docker Compose

## Running the Tests

### On Windows

Simply run the `run-tests.bat` script:

```
run-tests.bat
```

### On Linux/macOS

Run the following commands:

```bash
# Build the Docker image
docker-compose build

# Run the tests
docker-compose run --rm unit-tests
```

## Available Tests

1. **Main Unit Tests**: General functionality tests for the RaftCore components
2. **Device Type Records Tests**: Tests for the device identification functionality, including CRC validation

## Adding New Tests

To add new tests:

1. Create your test file in this directory (e.g., `NewFeatureTest.cpp`)
2. Update the `Makefile` to include your new test file:
   - Add a new variable for your test sources
   - Add a new variable for your test output binary
   - Add a new rule to compile your test
   - Update the `all` and `clean` targets

## Debugging

If you need to debug the build process or tests, you can run an interactive shell in the container:

```bash
docker-compose run --rm --entrypoint bash unit-tests
```

From there, you can manually run the build commands or inspect the filesystem.

## Test Structure

Each test file should:

1. Include the necessary headers
2. Define test cases
3. Implement a `main()` function that runs the tests
4. Print test results to the console 