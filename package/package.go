package cactor

import (
	cbase "github.com/jurgen-kluft/cbase/package"
	denv "github.com/jurgen-kluft/ccode/denv"
	ccore "github.com/jurgen-kluft/ccore/package"
	cunittest "github.com/jurgen-kluft/cunittest/package"
)

// GetPackage returns the package object of 'cactor'
func GetPackage() *denv.Package {
	// Dependencies
	cunittestpkg := cunittest.GetPackage()
	cbasepkg := cbase.GetPackage()
	ccorepkg := ccore.GetPackage()

	// The main (cactor) package
	mainpkg := denv.NewPackage("cactor")
	mainpkg.AddPackage(cunittestpkg)
	mainpkg.AddPackage(cbasepkg)
	mainpkg.AddPackage(ccorepkg)

	// 'cactor' library
	mainlib := denv.SetupCppLibProject("cactor", "github.com\\jurgen-kluft\\cactor")
	mainlib.AddDependencies(cbasepkg.GetMainLib()...)
	mainlib.AddDependencies(ccorepkg.GetMainLib()...)

	// 'cactor' unittest project
	maintest := denv.SetupDefaultCppTestProject("cactor_test", "github.com\\jurgen-kluft\\cactor")
	maintest.AddDependencies(cunittestpkg.GetMainLib()...)
	maintest.AddDependencies(cbasepkg.GetMainLib()...)
	maintest.AddDependencies(ccorepkg.GetMainLib()...)
	maintest.Dependencies = append(maintest.Dependencies, mainlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddUnittest(maintest)
	return mainpkg
}
