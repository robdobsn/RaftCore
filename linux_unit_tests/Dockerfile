FROM ubuntu:22.04

# Install required packages
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory
WORKDIR /app

# Copy the entire codebase
COPY .. /app/

# Create any missing directories that might be needed
RUN mkdir -p components/core/DeviceTypes \
    components/core/Bus \
    components/core/ArduinoUtils \
    components/core/Utils \
    components/core/RaftDevice \
    components/core/RaftJson

# Build the unit tests
WORKDIR /app/linux_unit_tests
# Remove the -k flag and the error suppression
RUN make

# Set the entrypoint to run the test binaries and propagate the exit code
ENTRYPOINT ["bash", "-c", "\
EXIT_CODE=0; \
if [ -f ./linux_unit_tests ]; then \
    echo 'Running linux_unit_tests...'; \
    ./linux_unit_tests || EXIT_CODE=1; \
else \
    echo 'ERROR: linux_unit_tests binary not built!'; \
    EXIT_CODE=1; \
fi; \
if [ -f ./device_type_tests ]; then \
    echo 'Running device_type_tests...'; \
    ./device_type_tests || EXIT_CODE=1; \
else \
    echo 'ERROR: device_type_tests binary not built!'; \
    EXIT_CODE=1; \
fi; \
exit $EXIT_CODE \
"] 