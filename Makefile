CNI_SRC := sknf-cni/src/args.c sknf-cni/src/cmd.c sknf-cni/src/err.c sknf-cni/src/io.c sknf-cni/src/ip.c sknf-cni/src/main.c sknf-cni/src/network.c sknf-cni/src/util.c
CNI_BIN := sknf-cni/bin/sknf-cni
CNI_CFLAGS := -O0 -g -Wall -Wno-parentheses
CNI_LDFLAGS := -static
CNI_PKG_CFLAGS := $(shell pkg-config --cflags libnl-3.0 libnl-route-3.0 json-c)
CNI_PKG_LIBS   := $(shell pkg-config --libs --static libnl-3.0 libnl-route-3.0 json-c)

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
	gcc $(CNI_CFLAGS) $(CNI_PKG_CFLAGS) -o $(CNI_BIN) $(CNI_SRC) $(CNI_LDFLAGS) $(CNI_PKG_LIBS)
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

.PHONY: kind-create-cluster
kind-create-cluster:
	@echo "[sknf] Creating kind cluster (no CNI, 1 control-plane + 2 workers)..."
	@printf '%s\n' \
		"kind: Cluster" \
		"apiVersion: kind.x-k8s.io/v1alpha4" \
		"networking:" \
		"  disableDefaultCNI: true" \
		"nodes:" \
		"- role: control-plane" \
		"- role: worker" \
		"- role: worker" \
	| kind create cluster --name sknf --config -
	@echo "[sknf] Kind cluster 'sknf' ready (CNI disabled)."

.PHONY: kind-deploy-image
kind-deploy-image:
	@echo "[sknf] Building Docker image..."
	docker build . -t $(IMAGE_NAME) -f $(DOCKERFILE)
	@echo "[sknf] Docker image built: $(IMAGE_NAME)"
	@echo "[sknf] Loading to kind..."
	kind load docker-image sknf:latest --name sknf
	@echo "[sknf] Loaded successfully."

.PHONY: k8s-deploy-resources
k8s-deploy-resources:
	@echo "[sknf] Deleting resources..."
	kubectl delete -f ./sknf-app/k8s/daemonset.yaml -f ./sknf-app/k8s/rbac.yaml || true
	@echo "[sknf] Loading resources..."
	kubectl apply -f ./sknf-app/k8s/rbac.yaml -f ./sknf-app/k8s/daemonset.yaml
	@echo "[sknf] Loaded successfully."

.PHONY: clean
clean:
	@echo "[sknf] Cleaning build artifacts..."
	rm -rf sknf-cni/bin sknf-app/bin
	@echo "[sknf] Clean complete."
