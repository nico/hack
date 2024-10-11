Path stroke
===========

Collects various things related to path stroking in HTML `<canvas>`, in SVG
documents, and in PDF documents.

Path close
----------

Details on how path closing and moveto commands are specced in HTML `<canvas>`,
in SVG documents, and in PDF documents.

### Canvas

<https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-closepath-dev>

> Marks the current subpath as closed, and starts a new subpath with a point
> the same as the start and end of the newly closed subpath.


<https://html.spec.whatwg.org/multipage/canvas.html#building-paths>

> Objects that implement the CanvasPath interface have a path. A path has a list
> of zero or more subpaths. Each subpath consists of a list of one or more
> points, connected by straight or curved line segments, and a flag indicating
> whether the subpath is closed or not. A closed subpath is one where the last
> point of the subpath is connected to the first point of the subpath by a
> straight line. Subpaths with only one point are ignored when painting the
> path.
> 
> [...]
> 
> When the user agent is to ensure there is a subpath for a coordinate (x, y) on
> a path, the user agent must check to see if the path has its need new subpath
> flag set. If it does, then the user agent must create a new subpath with the
> point (x, y) as its first (and only) point, as if the moveTo() method had been
> called, and must then unset the path's need new subpath flag.
> 
> The closePath() method, when invoked, must do nothing if the object's path has
> no subpaths. Otherwise, it must mark the last subpath as closed, create a new
> subpath whose first point is the same as the previous subpath's first point,
> and finally add this new subpath to the path.

### SVG

<https://www.w3.org/TR/SVG/paths.html#PathDataClosePathCommand>

> If a "closepath" is followed immediately by a "moveto", then the "moveto"
> identifies the start point of the next subpath. If a "closepath" is followed
> immediately by any other command, then the next subpath must start at the same
> initial point as the current subpath.

<https://www.w3.org/TR/SVG/paths.html#PathDataMovetoCommands>

> The "moveto" commands (M or m) must establish a new initial point and a new
> current point. The effect is as if the "pen" were lifted and moved to a new
> location. A path data segment (if there is one) must begin with a "moveto"
> command. Subsequent "moveto" commands (i.e., when the "moveto" is not the
> first command) represent the start of a new subpath


### PDF

#### operator h

[PDF 1.7 spec, p227](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf#page=227)

TABLE 4.9 Path construction operators

> Close the current subpath by appending a straight line segment
> from the current point to the starting point of the subpath. If the
> current subpath is already closed, h does nothing.
> This operator terminates the current subpath. Appending another
> segment to the current path begins a new subpath, even if the new
> segment begins at the endpoint reached by the h operation.


#### operator m

[PDF 1.7 spec, p226](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf#page=226)

TABLE 4.9 Path construction operators

> Begin a new subpath by moving the current point to coordinates
> (x, y), omitting any connecting line segment. If the previous path
> construction operator in the current path was also m, the new m
> overrides it; no vestige of the previous m operation remains in the
> path.

### TinyVG

Calls subpaths segments (see spec pdf, p15, "Close Path" at bottom).
It doesn't look like TinyVG has any stroke styles.

<https://github.com/TinyVG/specification/issues/4>

Degenerate paths
----------------

### Canvas

<https://html.spec.whatwg.org/multipage/canvas.html#trace-a-path>

Starts with:

> 1. Let path be a copy of the path being traced.
> 2. Prune all zero-length line segments from path.
> 3. Remove from path any subpaths containing no lines (i.e. subpaths with just
>    one point).

### SVG

<https://www.w3.org/TR/SVG/paths.html#ZeroLengthSegments>

> Path segments with zero length are not invalid, and will affect rendering in
> the following cases:
>
> * If markers are specified, then a marker is drawn on every applicable
>   vertex, even if the given vertex is the end point of a zero-length path
>   segment and even if "moveto" commands follow each other.
> * As mentioned in Stroke Properties, linecaps must be painted for zero-length
>   subpaths when stroke-linecap has a value of round or square.

### PDF

[PDF 1.7 spec, p231, 4.4.2 Path-Painting Operators, Stroking](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf#page=231):

> If a subpath is degenerate (consists of a single-point closed path or of two
> or more points at the same coordinates), the S operator paints it only if
> round line caps have been specified, producing a filled circle centered at
> the single point. If butt or projecting square line caps have been specified,
> S produces no output, because the orientation of the caps would be
> indeterminate. (This rule applies only to zero-length subpaths of the path
> being stroked, and not to zero-length dashes in a dash pattern. In the latter
> case, the line caps are always painted, since their orientation is determined
> by the direction of the underlying path.) A single-point open subpath
> (specified by a trailing m operator) produces no output.
