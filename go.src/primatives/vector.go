package primatives

type Vector2[T int32 | float64] struct {
	X, Y T
}
type Vector3[T int32 | float64] struct {
	X, Y, Z T
}
type Rectangle[T int32 | float64] struct {
	Vector2[T]
	Width, Height T
}
