#!/usr/bin/env python3

# Creates a jpeg2000 file with a palette, since I couldn't find any existing
# tool that does this :/

from PIL import Image
import argparse
import glymur
import numpy as np
import os

# requirements.txt:
# Pillow>=9.0.0
# numpy>=1.22.0
# glymur>=0.9.1

# To set up a new conda environment for this script:
#
# 1. Create the environment with the necessary packages listed in
#    requirements.txt:
#        conda create --name palettized_jp2_env python=3.9 -y
# 2. Activate the environment:
#        conda activate palettized_jp2_env
# 3. Install the dependencies using pip:
#        pip install -r requirements.txt
#
# Also need libopenjp2.dylib on search path:
#
#     export DYLD_LIBRARY_PATH=$PWD/../openjpeg/build/bin
#
# If you're using conda, don't run this as `./make_palettized_jpeg2000.py`
# as that will pick up system python instead of conda python. Run
# `python3 make_palettized_jpeg2000.py ...` instead. (Else, glymur will complain
# "You must have at least version 2.3.0 of OpenJPEG in order to write images."
# because it can't find libopenjp2.dylib.)

def create_palettized_jp2(image_path, output_path, num_colors=256,
                          macos_15_1_workaround=False):
    # 1. Create image
    palette = [
        255, 0, 0,
        0, 255, 0,
        0, 0, 255,
        0, 255, 255,
        255, 0, 255,
        255, 255, 0,
    ]
    image_data = np.array([[0, 1, 2], [3, 4, 5]], dtype=np.uint8)

    # 3. Create temporary grayscale JP2 with the palette indices.

    # Glymur seems to read the file if it exists. Delete it if it exists
    # to make sure glymur writes fresh files.
    try:
        os.remove('tmp.jp2')
    except:
        pass
    jp2 = glymur.Jp2k(
        'tmp.jp2',
        data=image_data,
        colorspace='gray',
        numres=1, # You can adjust this
    )

    # 4. Adjust boxes to turn it into a palettized jp2.
    #
    # Similar to rewrap_for_colormap() in
    # https://github.com/quintusdias/glymur/blob/master/glymur/tiff.py#L220
    #
    # If `tiff2jp2` worked, that would've been best! But I couldn't get it
    # to produce working output. I tried:
    #
    #     DYLD_LIBRARY_PATH=../tiff-4.7.0/libtiff/.libs:../openjpeg/build/bin \
    #         tiff2jp2 palette.tiff plaette.jp2

    # Find and update colr box to say this contains sRGB data
    # (colr stores state after palette application).
    boxes = jp2.box[:]
    jp2h_box = [box for box in boxes if box.box_id == 'jp2h'][0]

    colr_box = jp2h_box.box[1]  # colr box should be the second box
    assert colr_box.box_id == 'colr'
    colr_box.method = 1  # Enumerated color space
    colr_box.colorspace = 16 # sRGB

    # Create pclr box to store the palette
    num_palette_entries = len(palette) // 3

    if not all(p <= 127 for p in palette) and macos_15_1_workaround:
        # In macOS 15.1, Preview.app seems to not add 1 to the B^i field in the
        # pclr header, and errors out if any palette entry has a 127 < value <=
        # 255.  T.800 Table 1.13 makes it very clear that 1 should be added,
        # and everyone else does that, but the B^i text itself is a bit
        # unclear. Sigh.
        # If you don't care about Preview.app being able to open your file,
        # set this to 1. Else, this has to be 2 -- or keep the channel values
        # in the input image at < 127. (The image will look too dark everywhere
        # with this, including in Preview.app, but at least it'll show up.)
        # This is fixed in macOS 15.3. (I don't know the status in 15.2.)
        d = 2
        palette = [p // d for p in palette]

    palette = np.array(palette, dtype=np.uint8)
    palette = palette.reshape(num_palette_entries, 3)
    pclr_box = glymur.jp2box.PaletteBox(
        palette,
        [8] * palette.shape[1],
        [False] * palette.shape[1],
    )

    # Create cmap box to map 1st palette column to r, 2nd to g, 3rd to b.
    cmap_box = glymur.jp2box.ComponentMappingBox(
        component_index=(0, 0, 0),
        mapping_type=(1, 1, 1),
        palette_index=(0, 1, 2)
    )

    # Insert the new boxes after the colr box.
    jp2h_box.box.insert(2, pclr_box)
    jp2h_box.box.insert(3, cmap_box)

    # Rewrite grayscale jpeg2000 with new boxes.
    try:
        os.remove(output_path)
    except:
        pass
    jp2.wrap(output_path, boxes=boxes)

    print(f'Successfully created palettized JP2: {output_path}')


def main():
    parser = argparse.ArgumentParser(
        description='Create a palettized JPEG 2000 image.')
    parser.add_argument('image_path', help='path to input image file')
    parser.add_argument('output_path', help='path to output JP2 file')
    parser.add_argument('-c', '--colors', type=int, default=256,
                        help='Number of colors in the palette (default: 256)')
    parser.add_argument('--macos-15-1-workaround',
                        action=argparse.BooleanOptionalAction,
                        help='Make output openable on macOS 15.1 and earlier. '
                             'Darkens colors of output.')

    args = parser.parse_args()

    create_palettized_jp2(
        args.image_path,
        args.output_path,
        args.colors,
        args.macos_15_1_workaround,
    )

if __name__ == '__main__':
    main()
