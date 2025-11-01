CNI_SRC := sknf-cni/src/args.c sknf-cni/src/cmd.c sknf-cni/src/err.c sknf-cni/src/io.c sknf-cni/src/ip.c sknf-cni/src/main.c sknf-cni/src/network.c sknf-cni/src/util.c
CNI_BIN := sknf-cni/bin/sknf-cni
CNI_CFLAGS := -O0 -g -Wall -Wno-parentheses
CNI_LIBS := $(shell pkg-config --cflags --libs libnl-3.0 libnl-route-3.0 json-c)

APP_DIR := sknf-app
APP_BIN := $(APP_DIR)/bin/sknf-app
DOCKERFILE := $(APP_DIR)/Dockerfile
IMAGE_NAME := sknf

.PHONY: default
default: build-sknf-docker

.PHONY: build-sknf-cni
build-sknf-cni:
	@echo "[sknf] Building CNI plugin..."
	mkdir -p $(dir $(CNI_BIN))
	gcc $(CNI_CFLAGS) -o $(CNI_BIN) $(CNI_SRC) $(CNI_LIBS)
	@echo "[sknf] CNI plugin built at $(CNI_BIN)"

.PHONY: build-sknf-app
build-sknf-app:
	@echo "[sknf] Building DaemonSet app..."
	cd $(APP_DIR) && mkdir -p bin && go build -o bin/sknf-app .
	@echo "[sknf] App built at $(APP_BIN)"

.PHONY: build-sknf-docker
build-sknf-docker:
	@echo "[sknf] Building Docker image..."
	docker build . -t $(IMAGE_NAME) -f $(DOCKERFILE)
	@echo "[sknf] Docker image built: $(IMAGE_NAME)"

.PHONY: clean
clean:
	@echo "[sknf] Cleaning build artifacts..."
	rm -rf sknf-cni/bin sknf-app/bin
	@echo "[sknf] Clean complete."
