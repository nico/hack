Display of Images in Preview.app on Retina Screens
==================================================

Preview.app displays images at 2x zoom by default on retina screens.
But if an image has metadata that identifies it as having 144 DPI,
then Preview.app displays it at 1x zoom.

Here's how to create images with this metadata in various formats.

PNG
---

    sips -s dpiWidth 144 -s dpiHeight 144 -o out-dpi144.png out.png

This has the effect of wringing a `pHYs` chunk with the resolution for both X
and Y set to 5669 == 0x1625 dots per and the unit set to "meters" (1):


    00000009 70485973 00001625 00001625 01495224 F0
              p H Y s                    m (crc)

There are 39.3701 inch per meter, and

    5669 dots / meter = 5669 / 39.3701 dots / inch = 143.9925 DPI

That's as close to 144 DPI as PNG can store.

`open out-dpi144.png` now shows image at 1:1 pixel scale \o/


JPEG
----

Preview.app -> Tools -> Adjust Size… and then uncheck "Resample"
and set DPI to 144 does seem to do the trick. That does:

    % ~/src/hack/jpeg_exif_dump out-dpi144.jpg
    ...
    ffe1 at offset 2, size 4094: Application Segment (APP1)
      app id: 'Exif'
      tag 274 (Orientation) format 3 (unsigned short): count 1: 1 (top left)
      tag 282 (XResolution) format 5 (unsigned rational): count 1: 144/1 (144.000)
      tag 283 (YResolution) format 5 (unsigned rational): count 1: 144/1 (144.000)
      tag 296 (ResolutionUnit) format 3 (unsigned short): count 1: 2 (inch)
      tag 531 (YCbCrPositioning) format 3 (unsigned short): count 1: 1 (centered)
      tag 34665 (Exif IFD) format 4 (unsigned long): count 1: 102
      exif IFD
        tag 36864 (ExifVersion) format 7 (undefined): count 4 (02.21)
        tag 37121 (ComponentsConfiguration) format 7 (undefined): count 4 (YCbCr.)
        tag 40960 (FlashpixVersion) format 7 (undefined): count 4 (01.00)
        tag 40961 (ColorSpace) format 3 (unsigned short): count 1: 1 (sRGB)
        tag 40962 (PixelXDimension) format 4 (unsigned long): count 1: 800
        tag 40963 (PixelYDimension) format 4 (unsigned long): count 1: 802
        tag 41990 (SceneCaptureType) format 3 (unsigned short): count 1: 0 (Standard)

Doesn't write APP0, but does set XResolution, YResolution, ResolutionUnit which
seems to do the trick.

Converting a PNG with the right metadata also works:

    sips -s format jpeg -o out-dpi144-sips.jpg out-dpi144.png 

This writes APP0 + Exif:

    ffe0 at offset 2, size 16: Application Segment (APP0)
      app id: 'JFIF'
      jfif version: 1.1
      density unit: 0 (no units, aspect only)
      Xdensity: 144
      Ydensity: 144
      Xthumbnail: 0
      Ythumbnail: 0
    ffe1 at offset 20, size 116: Application Segment (APP1)
      app id: 'Exif'
      tag 282 (XResolution) format 5 (unsigned rational): count 1: 144/1 (144.000)
      tag 283 (YResolution) format 5 (unsigned rational): count 1: 144/1 (144.000)
      tag 296 (ResolutionUnit) format 3 (unsigned short): count 1: 2 (inch)
      tag 34665 (Exif IFD) format 4 (unsigned long): count 1: 78
      exif IFD
        tag 40962 (PixelXDimension) format 4 (unsigned long): count 1: 800
        tag 40963 (PixelYDimension) format 4 (unsigned long): count 1: 802
    ffed at offset 138, size 56: Application Segment (APP13)
      app id: 'Photoshop 3.0'
      Resource block 0x0404 (IPTC-NAA record) '' size 0
      Resource block 0x0425 (MD5 checksum) '' size 16
        checksum: d41d8cd9-8f00b204-e9800998-ecf8427e
    ffe2 at offset 196, size 4524: Application Segment (APP2)
      app id: 'ICC_PROFILE'
      chunk 1/1
      Profile size: 4508
      Preferred CMM type: 'appl'
      Profile version: 2.0.0.0
      Profile/Device class: 'mntr' (Display device profile)
      Data color space: 'GRAY' (Gray)
      Profile connection space (PCS): 'XYZ ' (nCIEXYZ or PCSXYZ)
      Profile created: 2012-08-23T15:46:15Z
      Profile file signature: 'acsp'
      Primary platform: 'APPL' (Apple Inc.)
      Profile flags: 0x00000000
      Device manufacturer: 'none'
      Device model: none
      Device attributes: 0x0000000000000000
      Rendering intent: 0 (Perceptual)
      PCS illuminant: X = 0.9642, Y = 1.0000, Z = 0.8249
      Profile creator: 'appl'
      Profile ID: not computed
      5 tags
        signature 'desc' (0x64657363): type 'desc' offset 192 size 121
          ascii: "Generic Gray Gamma 2.2 Profile"
          (no unicode data)
          (no scriptcode data)
        signature 'dscm' (0x6473636d): type 'mluc' offset 316 size 2074

However, using the technique used for PNGs above does *not* work:

    sips -s dpiWidth 144 -s dpiHeight 144 out-dpi144.jpg

    % ~/src/hack/jpeg_exif_dump out-dpi144.jpg
    ..
    ffe0 at offset 2, size 16: Application Segment (APP0)
      app id: 'JFIF'
      jfif version: 1.1
      density unit: 0 (no units, aspect only)
      Xdensity: 144
      Ydensity: 144
      Xthumbnail: 0
      Ythumbnail: 0
    ffe1 at offset 20, size 76: Application Segment (APP1)
      app id: 'Exif'
      tag 34665 (Exif IFD) format 4 (unsigned long): count 1: 26
      exif IFD
        tag 40961 (ColorSpace) format 3 (unsigned short): count 1: 1 (sRGB)
        tag 40962 (PixelXDimension) format 4 (unsigned long): count 1: 800
        tag 40963 (PixelYDimension) format 4 (unsigned long): count 1: 802

Opening a file like this with `open out-dpi144.jpg` does *not* show
the image at 1x zoom but at 2x zoom.


JPEG2000
--------

Even though JPEG2000 could store metadata for this, and both `sips` and
Preview.app can save JPEG2000 files, setting DPI to 144 and writing as
JPEG2000 does not seem to produce a file that shows at 1x zoom.

I tried:

- Setting DPI directly:

      sips -s format jp2 -s dpiWidth 144 -s dpiHeight 144 \
          -o out-dpi144-sips.jp2 out.png

- Converting a PNG with the right metadata (this worked for jpegs):

      sips -s format jp2 -o out-dpi144-sips.jp2 out-dpi144.png

- Preview.app -> Tools -> Adjust Size… and then uncheck "Resample"
  and set DPI to 144, then save in Preview.app (this also worked
  for jpegs)

Neither of these produce files that Preview.app shows at 1x zoom.

They all *do* have the effect of inserting these markers, according to
SerenityOS's `isobmff` tool:

      ('res ')
        ('resd')
        - vertical_capture_grid_resolution = 4719/32768 * 10^3
        - horizontal_capture_grid_resolution = 4719/32768 * 10^3

But they don't write exif info, and for jpegs that was what Preview.app looked
at, and I don't currently know how to create JPEG2000 files that have Exif
data with this.

WebP
----

Neither Preview.app nor `sips` can write WebP files.

WebP files *can* store Exif data, so maybe a WebP file with the rigth Exif
data would show up at 1x, but I don't currently know how to crate WebP files
that have Exif data with this.
