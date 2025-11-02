package main

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"strings"
	"syscall"

	"github.com/felipeek/sknf/sknf-app/internal/util"

	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
)

const NODE_NAME_ENV_KEY = "NODE_NAME"
const CNI_PLUGIN_BINARY_CONTAINER_PATH_ENV_KEY = "CNI_PLUGIN_BINARY_PATH"
const CNI_PLUGIN_CONF_CONTAINER_PATH_ENV_KEY = "CNI_PLUGIN_CONF_PATH"

const CNI_PLUGIN_BINARY_CONTAINER_PATH_DEFAULT = "sknf-cni/bin/sknf-cni"
const CNI_PLUGIN_CONF_CONTAINER_PATH_DEFAULT = "sknf-cni/conf/sknf-conf.json"

const CNI_PLUGIN_BINARY_HOST_PATH = "/host/opt/cni/bin/sknf-cni"
const CNI_PLUGIN_CONF_HOST_PATH = "/host/etc/cni/net.d/sknf-conf.json"

func main() {
	nodeName := os.Getenv(NODE_NAME_ENV_KEY)
	cniPluginBinaryContainerPath := os.Getenv(CNI_PLUGIN_BINARY_CONTAINER_PATH_ENV_KEY)
	cniPluginConfContainerPath := os.Getenv(CNI_PLUGIN_CONF_CONTAINER_PATH_ENV_KEY)

	if nodeName == "" {
		fmt.Fprintf(os.Stderr, "[sknf] Missing env var %s\n", NODE_NAME_ENV_KEY)
		os.Exit(1)
	}

	if cniPluginBinaryContainerPath == "" {
		cniPluginBinaryContainerPath = CNI_PLUGIN_BINARY_CONTAINER_PATH_DEFAULT
	}

	if cniPluginConfContainerPath == "" {
		cniPluginConfContainerPath = CNI_PLUGIN_CONF_CONTAINER_PATH_DEFAULT
	}

	// Load in-cluster configuration
	cfg, err := rest.InClusterConfig()
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure loading in-cluster config: %v\n", err)
		os.Exit(1)
	}

	clientset, err := kubernetes.NewForConfig(cfg)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure creating k8s client: %v\n", err)
		os.Exit(1)
	}

	podCidr, err := GetNodePodCidr(clientset, nodeName)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure acquiring node's pod CIDR: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("Node name: %s\n", nodeName)
	fmt.Printf("Pod CIDR: %s\n", podCidr)

	err = util.Copy(cniPluginBinaryContainerPath, CNI_PLUGIN_BINARY_HOST_PATH, 0o755)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure copying CNI plugin binary from %s to %s: %v\n",
			cniPluginBinaryContainerPath, CNI_PLUGIN_BINARY_HOST_PATH, err)
		os.Exit(1)
	}

	cniPluginConfTemplate, err := util.ReadFileToString(cniPluginConfContainerPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure opening CNI plugin conf template from %s: %v\n",
			cniPluginConfContainerPath, err)
		os.Exit(1)
	}

	cniPluginConfData := ReplaceSubnet(cniPluginConfTemplate, podCidr)

	err = util.WriteStringToFile(CNI_PLUGIN_CONF_HOST_PATH, cniPluginConfData)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure writing CNI configuration to %s: %v\n", CNI_PLUGIN_BINARY_HOST_PATH, err)
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

func GetNodePodCidr(clientset *kubernetes.Clientset, nodeName string) (string, error) {
	node, err := clientset.CoreV1().Nodes().Get(context.Background(), nodeName, metav1.GetOptions{})
	if err != nil {
		fmt.Fprintf(os.Stderr, "[sknf] Failure getting node %s: %v\n", nodeName, err)
		return "", fmt.Errorf("failed to get node %s: %v", nodeName, err)
	}

	if node.Spec.PodCIDR != "" {
		return node.Spec.PodCIDR, nil
	} else if len(node.Spec.PodCIDRs) == 1 {
		return node.Spec.PodCIDRs[0], nil
	} else if len(node.Spec.PodCIDRs) > 0 {
		// TODO: Implement this
		fmt.Fprintf(os.Stderr, "[sknf] Detected multiple pod CIDRs %v in node %s\n", node.Spec.PodCIDRs, nodeName)
		return "", fmt.Errorf("detected multiple pod CIDRs %v in node %s, this is not implemented", node.Spec.PodCIDRs, nodeName)
	}

	fmt.Fprintf(os.Stderr, "[sknf] Missing Pod CIDR for node %s\n", nodeName)
	return "", fmt.Errorf("missing pod CIDR for node %s", nodeName)
}
