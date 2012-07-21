dm3tool
=======

A (very incomplete) tool to read the
[dm3 image format](http://rsbweb.nih.gov/ij/plugins/DM3Format.gj.html).


    $ ./dm3tool -h
    usage: dm3tool [options] inputfile.dm3

    options:
      -d       dump file tree to stdout
      -v       verbose output


Image data seems to be in

    [
      'ImageList': [
        {
          'ImageData': {
            'Data': []
            'DataType': number,
            'Dimensions': [w, h],
            'PixelDepth': number,
          }
        }
        'Name': array of ushort (utf16 data)
      ]
    ]
