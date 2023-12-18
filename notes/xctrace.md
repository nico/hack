# `xctrace` notes

Record:

```sh
xctrace record --template 'Time Profiler' --target-stdout - \
    --launch --output trace.trace -- /usr/bin/python3 -c 'print("hello")'
```

Get TOC:

```sh
xctrace export --input trace.trace --toc  ```
```

Export samples:

```sh
xctrace export --input trace.trace \
    --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' \
    | mvim -
```

Use e.g.

```py
#!/usr/bin/env python3

import xml.etree.ElementTree

with open('foo.xml') as f:
  xml_text = f.read()

element = xml.etree.ElementTree.XML(xml_text)
xml.etree.ElementTree.indent(element)
print(xml.etree.ElementTree.tostring(element, encoding='unicode'))
```

for dumping the XML.

The `kdebug` schema is also interesting (see macperf folder in this repo).

The `dyld-library-load` schema has some, but not all (?) load addresses.

The `time-sample` schema is also interesting.

The `kdebug-strings` schema has all the paths to all loaded binaries
(with `id=`; probably `ref=`'d from elsewhere?). `kdebug-class` values are
from `/usr/include/sys/kdebug.h` in the SDK (`DBG_*` for the classes).

`--xpath '/trace-toc/run[@number="1"]/data/table'` dumps everything.

## See also

* `sample`
* `ktrace` (apparently back, after being replaced by `dtrace`?)
* <https://github.com/google/instrumentsToPprof>
