/*
pdfobject ::= Null
| Boolean of bool  `true`, `false`
| Integer of int   123 43445 +17 −98 0
| Real of real     34.5 −3.62 +123.6 4. −.002 0.0
| String of string (literal string), <hexstring>
| Name of string
| Array of pdfobject array
| Dictionary of (string, pdfobject) array Array of (string, pdfobject) pairs
| Stream of (pdfobject, bytes) Stream dictionary and stream data
| Indirect of int
*/

/*
1. Read the PDF header from the beginning of the file, checking that this is,
indeed, a PDF document and retrieving its version number.

1b. Check for linearization dictionary after header.

2. The end-of-file marker is now found, by searching backward from the end of
the file. The trailer dictionary can now be read, and the byte offset of the
start of the cross-reference table retrieved.

3. The cross-reference table can now be read. We now know where each object in
the file is.

4. At this stage, all the objects can be read and parsed, or we can leave this
process until each object is actually needed, reading it on demand.

5. We can now use the data, extracting the pages, parsing graphical content,
extracting metadata, and so on.
*/
int parse_pdf() {

  // 1. Header
  // %PDF-1.0
  // %nonasciichars

  // 2. Body
  // `objnum` gennum obj
  //   (any pdfobject)
  // `endobj`

  // 3. Cross-reference table
  // `xref`
  // 0 n
  // 0000000000 65535 f
  // 0000000015 00000 `n`   n times (offset, size, `n`)

  // 4. Trailer
  // `trailer`

  // 5. %%EOF
}

