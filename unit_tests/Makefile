ROOTDIR = $(realpath $(CURDIR)/..)
WORKING_DIR = $(realpath $(CURDIR))
DOCKER ?= docker run --rm -v $(ROOTDIR):$(ROOTDIR) -w $(WORKING_DIR) espressif/idf:v5.0.2
CMD ?= idf.py -B ./build build

all: build

build: Makefile $(wildcard main/*) $(wildcard components/core/*)
	$(DOCKER) $(CMD)

clean:
	$(DOCKER) rm -rf build sdkconfig || true
