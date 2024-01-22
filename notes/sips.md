sips
====

macOS includes a command-line tool `sips` that can do many operations on
images.

However, it's awkward to use, and even with looking at its man page, it
always takes me a while to figure out how to get it to do what I want.

So here's a list of example invocations.

## Convert a png to a jpeg:

    sips -s format png -o out.png in.jpg

Also pass `-s formatOptions 45` to save with jpeg quality 45 (out of 100).

## Strip an embedded ICC profile, in-place:

    sips -d profile inout.jpg

(use `-o out.jpg` to not do it in place but to write the output to a different
file)

## Convert an image to different color space:

    sips -o out.png --matchTo serenity-sRGB.icc file-in-p3.png
