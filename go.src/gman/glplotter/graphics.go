package glplotter

import "odbm/primatives"

type DrawAction uint

const (
	DrawActionSleep DrawAction = 1
	DrawActionInvalidate
	DrawActionAnimate
)

type Viewport primatives.Rectangle[int32]

type Graphic interface {
	GetDrawAction() DrawAction
	GetViewport() Viewport
	Draw()
}

func AddGraphic(graphic Graphic) {
	
	graphics = append(graphics, graphic)
}

var graphics []Graphic
