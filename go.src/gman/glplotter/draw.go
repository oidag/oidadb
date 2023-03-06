package glplotter

import (
	"fmt"
	"github.com/go-gl/gl/all-core/gl"
	"os"
)

// will count up to 39916800 (11!, which means its divisable
// by all numbers below 11 and combinitaitons thereof) before resetting back to
// 1.
var FrameID uint = 1
var FrameIDd float64 = 1

// returns -2: stop drawing all together and close the window, window_render will return 0.
// returns -1: same as -2 but will have window_redner return -1.
// returns 0: draw-sleep. Don't execute cb again unless on an event
// retunrs 1: draw-fire. Draw again.
//
// The call back will be executed whenever the hell it wants to. Its up to you
// to find out what exactly needs to be redrawn for each draw.
func draw() (ret int) {
	FrameID++
	if FrameID == 39916801 {
		FrameID = 1
	}
	FrameIDd = (float64)(FrameID)

	// for each graphic, we only need to redraw it if its bounding box is either
	// invalidated, or is sitting on top of a bounding box that is invalided.
	//
	// We do not need to redraw a graphic if the contents below it had not changed.
	//
	// Now you may ask, "well, what If I move a box over a circle, sure the circle doesn't
	// need to redraw then, but if I move the box somewhere else then the circle would
	// remain stained with the box on top of it?". What we do is just draw back the image
	// data the sleeper submitted back on the boundingbox of that box.
	//
	// If a graphic is redrawn, regardless if its marking itself as sleeping, the bounding
	// box for the newly drawn graphic will be marked as invalidated for the
	// items potential above it for this frame. This is because if a graphic is under another,
	// and the sub graphic is redrawn, it would redraw itself ontop of the super
	// ficial graphics.
	for _, g := range graphics {

		// todo: adapt the invalidation logic. Until then just draw everything constantly.
		//       fuck it.
		gl.ClearColor(0, 0, 0, 0)
		gl.Clear(gl.COLOR_BUFFER_BIT)

		g.GetDrawAction()

		// set the view port
		v := g.GetViewport()
		gl.Viewport(v.X, v.Y, v.Width, v.Height)
		gl.PushMatrix()
		glerror := gl.GetError()
		if glerror != 0 {
			fmt.Fprintf(os.Stderr, "pre draw() glError")
		}
		g.Draw()
		glerror = gl.GetError()
		if glerror != 0 {
			fmt.Fprintf(os.Stderr, "post draw() glError")
		}
		gl.PopMatrix()
		w, h := window.GetSize()
		gl.Viewport(0, 0, int32(w), int32(h))

	}
	return 1
}
