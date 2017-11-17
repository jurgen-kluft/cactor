package main

import (
	"github.com/jurgen-kluft/xcode"
	"github.com/jurgen-kluft/xactor/package"
)

func main() {
	xcode.Generate(xactor.GetPackage())
}
