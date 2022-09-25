EXIF in jpeg files
==================

under-constuction.gif

This is about metadata in jpeg files.

JPEG file structure
-------------------

A JPEG file is a sequence of "markers" (...for the most part, except for the
actual compressed image data).

A marker is the byte `0xff` followed by another byte, the marker ID. The second
byte should not be `0xff` (sequences of multiple `0xff` bytes are used for
padding, so skip all but the last of a `0xff` sequence) and it should not be
`0` (a `0xff` followed by a `0` is how an actual `0xff` data byte is escaped as
in the actual compressed data).

Except for three exceptions described below, every marker is then followed by a
big-endian `uint16_t` that stores how many bytes of data follow the marker.
This size field includes the size of the size field itself, but not of the
marker bytes. In other words, the size field is always at least `2` if present.

This means that the size of each marker's data is limited to `65535 - 2` bytes.

The meaning of the data in each marker is marker-specific.

The three markers _without_ a size field are:

1. Marker ID `0xd8` is "start of image" (often abbreviated "SOI"), and every
   jpeg file starts with it. That is, the first two bytes of a jpeg file are
   always `0xff 0xd8`. This marker ends immediately, it's not followed by any
   data belonging to the marker. (It _is_ followed by a different marker.)

2. Marker ID `0xd9` is "end of image" ("EOI"). This is the last marker in jpeg
   file.  This marker is also not followed by any data belonging to itself.
   Bytes following this marker are called the "trailer". Decoders are supposed
   to ignore data in the trailer, and some JPEG extensions put data there.
   But often, it's indeed empty and many JPEG files end after it.

3. Marker IDs `0xd0` - `0xd7` are "Restart Markers" ("RST"). These are optional
   markers that can be embedded in the main image data to mark points in the
   data stream where internal decoder states are "clean". They were originally
   intended to mark places where it's possible to restart encoding if part
   of the data stream is corrupted. Nowadays they can be used to split the
   compressed data into independent chunks that can e.g. decompressed on
   separate threads.

There are quite a few decent guides on how to actually decode JPEG image data
(e.g. [here][1], [here][2], [here][3], or [here][4]), so this won't talk about
that part.  As a very short summary, there are a few markers to:
* define quantization tables (`0xdb`, "DQT")
* start a "frame" ("SOF"): This stores the width and height of the decompressed
  image, the bits per pixel, and how many components (channels) the file has
* define huffman tables (`0xc4`, "DHT")
* start a "scan" ("SOS"): This stores which huffman table for each table, and
  it is _followed_ immediately by the compressed image data. The compressed
  image data is not part of the SOS marker's size, which is why the actual
  image data can be larger than `65535 - 2` bytes. To find the next marker
  after the image data, scan for `0xff` followed by a non-`0` byte. The image
  data can contain embedded "Restart Markers". The next non-RST marker after
  the image data is often "EOI", but not always.

[1]: https://www.ccoderun.ca/programming/2017-01-31_jpeg/
[2]: https://koushtav.me/jpeg/tutorial/c++/decoder/2019/03/02/lets-write-a-simple-jpeg-library-part-2/
[3]: https://parametric.press/issue-01/unraveling-the-jpeg/
[4]: https://yasoob.me/posts/understanding-and-writing-jpeg-decoder-in-python/
 
Metadata, and thumbnails
------------------------

Markers `0xe0` - `0xef` are "Application Segment" ("APPn": `0xe0` is "APP0",
`0xe1` is "APP1", and so on, with `0xef` finally being "APP15") markers. Their
size is followed by a `\0`-terminated string, and applications can put
arbitrary application-specific data in there.

Certain APP/string combinations are standardized, others are very common.

The "JFIF" standard dictates that the SOI marker is immediately followed by
an APP0 marker with a type string of "JFIF\0". This marker stores the JFIF
version, the image's pixel density (either as pixels per inch, pixels per cm),
and an optional thumbnail image that's at most 255x255 pixel whose pixel data
is stored as uncompressed 24-bit RGB in the marker's data.  Due to the `65535`
max marker data size, this means a thumbnail that's 255 pixels wide can at most
be 85 pixels high. See [here][5] for details on the encoding of this segment.

XXX:

'JFIF' thumbnail never used in practice

JFXX thumbnail (can be jpeg)

The "EXIF" standard dictates that the SOI marker is immediately followed by
an APP1 marker with a type string of "EXIF\0".

EXIF thumbnail (can be jpeg, but kind of crowded segment)
(max 160x120 per R998)

APP2 / 'MPF\0'

[5]: https://en.wikipedia.org/wiki/JPEG_File_Interchange_Format#JFIF_APP0_marker_segment


