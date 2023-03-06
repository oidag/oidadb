package glplotter

import (
	"github.com/go-gl/gl/all-core/gl"
	"github.com/go-gl/glfw/v3.3/glfw"
)

var window *glfw.Window

func windowinit(text string, w, h int) error {
	err := glfw.Init()
	if err != nil {
		return err
	}
	window, err = glfw.CreateWindow(w, h, text, nil, nil)
	if err != nil {
		return err
	}
	window.SetPos(2560*2+100, 100)
	window.MakeContextCurrent()
	err = gl.Init()
	if err != nil {
		return err
	}

	return nil
}

func Init() error {
	err := windowinit("the tool", 1200, 800)
	if err != nil {
		return err
	}

	initevents()

	gl.Enable(gl.BLEND)
	gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA)
	//gl.MatrixMode(gl.MODELVIEW)

	return nil
}

func initevents() {

}
