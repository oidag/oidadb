package main

import (
	"odbm/gman"
	"odbm/gman/glplotter"
	"runtime"
)

func init() {
	// This is needed to arrange that main() runs on main thread.
	// See documentation for functions that are only allowed to be called from the main thread.
	runtime.LockOSThread()
}

func main() {
	err := glplotter.Init()
	if err != nil {
		panic(err)
	}

	gman.Debug.Init()

	glplotter.Serve()

}
