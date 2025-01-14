/*
 * Copyright 2024 Redpanda Data, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */

package main

import (
	"os"
	"slices"
	"strings"
	"syscall"
)

var (
	ldLibraryPath = ""
	binaryPath    = ""
)

func main() {
	path := "PATH=/opt/redpanda/bin"
	existingPath, ok := os.LookupEnv("PATH")
	if ok {
		path += string(os.PathListSeparator) + existingPath
	}
	env := slices.DeleteFunc(os.Environ(), func(envvar string) bool { return strings.HasPrefix(envvar, "PATH=") })
	env = append(env, path)
	if ldLibraryPath != "" {
		env = append(env, "LD_LIBRARY_PATH="+ldLibraryPath)
	}
	syscall.Exec(binaryPath, os.Args, env)
}
