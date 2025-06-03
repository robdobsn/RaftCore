@echo off
echo Building and running unit tests using Docker...
cd %~dp0
docker-compose build --progress=plain
docker-compose run --rm unit-tests
if %ERRORLEVEL% neq 0 (
    echo ERROR: Tests failed with exit code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
) else (
    echo All tests completed successfully!
) 