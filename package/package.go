package cactor

import (
	cbase "github.com/jurgen-kluft/cbase/package"
	denv "github.com/jurgen-kluft/ccode/denv"
	cunittest "github.com/jurgen-kluft/cunittest/package"
)

const (
	repo_path = "github.com\\jurgen-kluft\\"
	repo_name = "cactor"
)

func GetPackage() *denv.Package {
	name := repo_name

	// dependencies
	cunittestpkg := cunittest.GetPackage()
	cbasepkg := cbase.GetPackage()

	// main package
	mainpkg := denv.NewPackage(repo_path, repo_name)
	mainpkg.AddPackage(cunittestpkg)
	mainpkg.AddPackage(cbasepkg)

	// main library
	mainlib := denv.SetupCppLibProject(mainpkg, name)
	mainlib.AddDependencies(cbasepkg.GetMainLib())

	// test library
	testlib := denv.SetupCppTestLibProject(mainpkg, name)
	testlib.AddDependencies(cbasepkg.GetTestLib())

	// unittest project
	maintest := denv.SetupCppTestProject(mainpkg, name)
	maintest.AddDependencies(cunittestpkg.GetMainLib())
	maintest.AddDependencies(cbasepkg.GetMainLib())
	maintest.AddDependency(mainlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddTestLib(testlib)
	mainpkg.AddUnittest(maintest)
	return mainpkg
}
