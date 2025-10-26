package main

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"strings"
	"syscall"

	"github.com/felipeek/sknf/sknf-app/internal/util"
)

const NODE_NAME_ENV_KEY = "NODE_NAME"
const POD_CIDR_ENV_KEY = "POD_CIDR"
const CNI_PLUGIN_BINARY_CONTAINER_PATH_ENV_KEY = "CNI_PLUGIN_BINARY_PATH"
const CNI_PLUGIN_CONF_CONTAINER_PATH_ENV_KEY = "CNI_PLUGIN_CONF_PATH"

const NODE_NAME_DEFAULT = "node-dummy"
const POD_CIDR_DEFAULT = "10.0.0.0/16"
const CNI_PLUGIN_BINARY_CONTAINER_PATH_DEFAULT = "sknf-cni/bin/sknf-cni"
const CNI_PLUGIN_CONF_CONTAINER_PATH_DEFAULT = "sknf-cni/conf/sknf-conf.json"

const CNI_PLUGIN_BINARY_HOST_PATH = "/host/opt/cni/bin/sknf-cni"
const CNI_PLUGIN_CONF_HOST_PATH = "/host/etc/cni/net.d/sknf-conf.json"

func main() {
	nodeName := os.Getenv(NODE_NAME_ENV_KEY)
	podCidr := os.Getenv(POD_CIDR_ENV_KEY)
	cniPluginBinaryContainerPath := os.Getenv(CNI_PLUGIN_BINARY_CONTAINER_PATH_ENV_KEY)
	cniPluginConfContainerPath := os.Getenv(CNI_PLUGIN_CONF_CONTAINER_PATH_ENV_KEY)

	if nodeName == "" {
		nodeName = NODE_NAME_DEFAULT
	}

	if podCidr == "" {
		podCidr = POD_CIDR_DEFAULT
	}

	if cniPluginBinaryContainerPath == "" {
		cniPluginBinaryContainerPath = CNI_PLUGIN_BINARY_CONTAINER_PATH_DEFAULT
	}

	if cniPluginConfContainerPath == "" {
		cniPluginConfContainerPath = CNI_PLUGIN_CONF_CONTAINER_PATH_DEFAULT
	}

	fmt.Printf("Node name: %s\n", nodeName)
	fmt.Printf("Pod CIDR: %s\n", podCidr)

	err := util.Copy(cniPluginBinaryContainerPath, CNI_PLUGIN_BINARY_HOST_PATH)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure copying CNI plugin binary from %s to %s: %s\n",
			cniPluginBinaryContainerPath, CNI_PLUGIN_BINARY_HOST_PATH, err)
		os.Exit(1)
	}

	cniPluginConfTemplate, err := util.ReadFileToString(cniPluginConfContainerPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure opening CNI plugin conf template from %s: %s\n",
			cniPluginConfContainerPath, err)
		os.Exit(1)
	}

	cniPluginConfData := ReplaceSubnet(cniPluginConfTemplate, podCidr)

	err = util.WriteStringToFile(CNI_PLUGIN_CONF_HOST_PATH, cniPluginConfData)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure writign CNI configuration to %s: %s\n", CNI_PLUGIN_BINARY_HOST_PATH, err)
		os.Exit(1)
	}

	fmt.Println("[sknf] Install complete; entering wait loop")

	// Handle SIGTERM/SIGINT for clean shutdowns
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer stop()
	<-ctx.Done()
	fmt.Println("[sknf] Received shutdown signal, exiting")
}

func ReplaceSubnet(text, subnet string) string {
	return strings.ReplaceAll(text, "{{SUBNET}}", subnet)
}
