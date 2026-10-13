#pragma once

// Module "area": areas of parallelograms and triangles in R^2 by determinants,
// and how a linear transformation T(x) = Ax scales them.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// Labels for the prose the explanation emits. The caller owns the strings and
// they must outlive the Explanation; a null field falls back to English.
struct AreaLabels {
		const char* vectors = nullptr;        // "Columns are the edge vectors"
		const char* vertices = nullptr;       // "Columns are the vertices"
		const char* opposite = nullptr;       // "Find the corner opposite P1"
		const char* edges = nullptr;          // "Edge vectors from P1"
		const char* area = nullptr;           // "Area is |det| of the edge vectors"
		const char* half = nullptr;           // "A triangle is half the parallelogram"
		const char* area_word = nullptr;      // "Area", inside \text{}
		const char* images = nullptr;         // "Images of the edge vectors"
		const char* image_vertices = nullptr; // "T(P) is spanned by Au and Av"
		const char* area_image = nullptr;     // "Area of T(P) is |det| of Au, Av"
		const char* det_a = nullptr;          // "det(A)"
		const char* check = nullptr;          // "Check: Area T(P) = |det A| Area P"
};

enum class AreaShape : std::uint8_t {
		Parallelogram,
		Triangle,
};

// A region of R^2 as a corner and the two edge vectors from it. The
// parallelogram has vertices origin, origin+u, origin+u+v, origin+v; the
// triangle origin, origin+u, origin+v.
struct AreaRegion {
		Rational origin[2]{};
		Rational u[2]{};
		Rational v[2]{};
		std::uint8_t vertices = 0; // columns given as vertices: 0 (edge vectors), 3 or 4
		std::uint8_t u_col = 1;    // vertex columns that gave u and v
		std::uint8_t v_col = 2;
		std::uint8_t opposite = 0; // 4 vertices: the column opposite column 0
};

// Reads a region from the columns of the 2 x k matrix `p`:
//
//   2x2  the edge vectors u, v from the origin
//   2x3  three vertices; the first is the corner the other two are next to
//        (any corner works for the area)
//   2x4  a parallelogram's four vertices in any order (Parallelogram only)
//
// NotParallelogram when four points are not the corners of a parallelogram.
ErrorCode area_region(In MatrixView p, In AreaShape shape, Out AreaRegion* out) noexcept;

// The vertices of the region in drawing order, as the columns of `out`
// (2x4 for a parallelogram, 2x3 for a triangle). With `a` (2x2) given, the
// vertices of its image T(region) instead.
ErrorCode area_polygon(In const AreaRegion& r, In AreaShape shape, In const MatrixView* a, Out MatrixMutView out) noexcept;

// Area of the parallelogram or triangle given by the columns of `p` (see
// area_region): |det[u v]|, halved for a triangle.
//
// The explanation shows the input, the corner opposite the first vertex (four
// vertices), the edge vectors (vertex input), then the determinant. A
// `polygon` with data receives the vertices as area_polygon writes them.
Error op_area(In MatrixView p,
        In AreaShape shape,
        InOut Arena& scratch,
        Out Rational* area,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const AreaLabels& labels = AreaLabels{},
        Out MatrixMutView polygon = MatrixMutView{}) noexcept;

// T(x) = Ax for 2x2 `a` maps the parallelogram P given by the columns of `p`
// (see area_region) to the parallelogram T(P) with corner A origin and edges
// Au, Av. The explanation finds P and its area, the images Au, Av, the
// vertices of T(P), its area |det[Au Av]|, det(A), and checks
// Area T(P) = |det A| Area P: `det_a`, `area_p` and `area_tp` receive det(A),
// the area of P and the area of T(P) = |det A| area of P. `polygon_p` and `polygon_tp` (2x4), when they
// have data, receive the vertices of P and T(P).
Error op_area_transform(In MatrixView a,
        In MatrixView p,
        InOut Arena& scratch,
        Out Rational* det_a,
        Out Rational* area_p,
        Out Rational* area_tp,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const AreaLabels& labels = AreaLabels{},
        Out MatrixMutView polygon_p = MatrixMutView{},
        Out MatrixMutView polygon_tp = MatrixMutView{}) noexcept;

} // namespace matrix_core
