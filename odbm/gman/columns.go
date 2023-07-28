package gman

import (
	"odbm/gman/glplotter"
	"odbm/primatives"
)

type Column struct {
	primatives.Vector2[int32]
	Height int32
	Color  primatives.Vector3[float64]

	shards []Shard
}

// ifaces
var _ glplotter.Graphic = &Column{}

func (c Column) GetDrawAction() glplotter.DrawAction {
	//TODO implement me
	panic("implement me")
}

func (c Column) GetViewport() glplotter.Viewport {
	//TODO implement me
	panic("implement me")
}

func (c Column) Draw() {
	//TODO implement me
	panic("implement me")
}
