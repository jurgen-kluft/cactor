package main

import (
	cpkg "github.com/jurgen-kluft/cactor/package"
	ccode "github.com/jurgen-kluft/ccode"
)

func main() {
	if ccode.Init() {
		pkg := cpkg.GetPackage()
		ccode.GenerateFiles(pkg)
		ccode.Generate(pkg)
	}
}
