package gman

import (
	"github.com/go-gl/gl/all-core/gl"
	glp "odbm/gman/glplotter"
	gltext "odbm/gman/text"
	"os"
)

type debug struct {
	font *gltext.Font
}

func (d *debug) GetDrawAction() glp.DrawAction {
	//TODO implement me
	return glp.DrawActionAnimate
}

func (d *debug) GetViewport() glp.Viewport {
	//TODO implement me
	var vp glp.Viewport
	vp.X = 0
	vp.Y = 0
	vp.Width = 100
	vp.Height = 100
	return vp
}

func (d *debug) Draw() {
	//TODO implement me

	gl.Begin(gl.QUADS)
	gl.Color3d(1, 0, 0)
	gl.Vertex2d(-1, -1)
	gl.Vertex2d(1, -1)
	gl.Vertex2d(1, 1)
	gl.Vertex2d(-1, 1)
	gl.End()

	gl.Color3d(0, 1, 0)
	d.font.Printf(0, 0, "W")
}

var _ glp.Graphic = &debug{}

var Debug *debug = &debug{}
var (
	monospace_f = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
	sans_f      = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
	sansbold_f  = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
	serif_f     = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
)

func (d *debug) Init() {
	glp.AddGraphic(d)
	//var err error
	reader, err := os.Open(sansbold_f)
	if err != nil {
		panic(err)
	}
	d.font, err = gltext.LoadTruetype(reader, 100, 32, 127, gltext.LeftToRight)
	if err != nil {
		panic(err)
	}
	reader.Close()
}
