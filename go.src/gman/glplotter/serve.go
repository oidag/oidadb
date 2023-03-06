package glplotter

import (
	"errors"
	"github.com/go-gl/glfw/v3.3/glfw"
)

func Serve() (err error) {

	for !window.ShouldClose() {
		ret := draw()
		window.SwapBuffers()
		switch ret {
		case -1:
			err = errors.New("bad exit byt draw")
			fallthrough
		case -2:
			window.SetShouldClose(true)
			glfw.PollEvents()
		case 0:
			glfw.WaitEvents()
		case 1:
			glfw.PollEvents()
		}
	}
	return err
}
