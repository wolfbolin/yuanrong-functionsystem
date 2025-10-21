/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Package cmd is cmd of YuanRong
package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"

	"functionsystem/pkg/cli/cmd/start"
	"functionsystem/pkg/cli/cmd/status"
	"functionsystem/pkg/cli/cmd/stop"
	"functionsystem/pkg/cli/cmd/version"
	"functionsystem/pkg/cli/constant"
	"functionsystem/pkg/cli/pkg/cmdio"
)

// NewCmdRoot new root cmd
func NewCmdRoot() *cobra.Command {
	initYuanRongDeployAndInstallVars()
	cmd := &cobra.Command{
		Use:   fmt.Sprintf("%s", constant.CliName),
		Short: fmt.Sprintf("used on %s Platform", constant.PlatformName),
		Long:  fmt.Sprintf("used on %s Platform.", constant.PlatformName),
		Example: fmt.Sprintf(`
    %s start --master
    %s stop
    %s version
    %s status
    source <(%s completion bash)
		`, constant.CliName, constant.CliName, constant.CliName, constant.CliName, constant.CliName),
	}

	cio := cmdio.System()

	cobra.OnInitialize()

	cmd.AddCommand(start.InitYrCMD(cio))
	cmd.AddCommand(stop.InitYrCMD(cio))
	cmd.AddCommand(status.InitYrCMD(cio))
	cmd.AddCommand(version.InitVersionCMD(cio))

	return cmd
}

func initYuanRongDeployAndInstallVars() {
	// 1. use default values
	// Do nothing, already did

	// 2. from environment
	if installDir := os.Getenv(constant.EnvYuanRongInstallationDir); installDir != "" {
		constant.YuanRongInstallationDir = installDir
	}
	if deployDir := os.Getenv(constant.EnvYuanRongDeployDir); deployDir != "" {
		constant.YuanRongDeployDir = deployDir
	}
}
