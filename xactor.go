package main

import (
	"github.com/jurgen-kluft/xactor/package"
	"github.com/jurgen-kluft/xcode"
)

func main() {
	xcode.Init()
	xcode.Generate(xactor.GetPackage())
}
