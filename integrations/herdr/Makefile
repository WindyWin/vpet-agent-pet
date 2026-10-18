.DEFAULT_GOAL := build

VERSION ?= 0.17.1
LDFLAGS := -s -w -X main.version=$(VERSION)

.PHONY: build test clean install

build:
	mkdir -p bin
	CGO_ENABLED=0 go build -trimpath -ldflags "$(LDFLAGS)" -o bin/herdr-agent-pet ./cmd/herdr-agent-pet

test:
	go test -v ./...

clean:
	rm -rf bin

install:
	sh scripts/install.sh
