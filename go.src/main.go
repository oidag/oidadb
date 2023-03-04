package main

import "github.com/go-gl/glfw/v3.3/glfw"

func main() {
	println("d")
	err := glfw.Init()
	if err != nil {
		panic(err)
	}

	w, err := glfw.CreateWindow(1200, 800, "test", nil, nil)
	if err != nil {
		panic(err)
	}

	w.SetPos(2560*2+100, 100)
	w.MakeContextCurrent()

	for !w.ShouldClose() {
		w.SwapBuffers()
		glfw.PollEvents()
	}

}
