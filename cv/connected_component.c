#include "connected_component.h"

#include "graymap.h"

#include <stdlib.h>

static unsigned find(unsigned i, unsigned* id) {
  while (i != id[i]) {
    id[i] = id[id[i]];
    i = id[i];
  }
  return i;
}

static void union_(unsigned p, unsigned q, unsigned* id, unsigned* sz) {
  unsigned i = find(p, id);
  unsigned j = find(q, id);
  if  (sz[i] < sz[j]) { id[i] = j; sz[j] += sz[i]; } 
  else                { id[j] = i; sz[i] += sz[j]; }  
}

#include <stdio.h>

void find_biggest_connected_component(graymap_t* graymap) {
  const int w = graymap->w, h = graymap->h;
  const unsigned n = w*h;
  const uint8_t* prev = 0,
               * curr = graymap->data;

  unsigned * components = malloc(n*sizeof(unsigned));
  for (unsigned i = 0; i < n; ++i) components[i] = i;
  unsigned* sz = malloc(n*sizeof(unsigned));
  for (unsigned i = 0; i < n; ++i) sz[i] = 1;

  unsigned* prev_component = 0,
          * curr_component = components;
  unsigned max_component = 0, max_component_size = 0;

  // First pass: Find equivalence classes, keep track of the biggest class.
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (curr[x] != 0) continue;

      if (x > 0 && curr[x-1] == 0)
        union_(curr_component[x], curr_component[x-1], components, sz);
      if (prev && prev[x] == 0) {
        // curr[x] and prev[x] could already be in the same class. sz assumes
        // that union_() is never called for two elements that are already in
        // the same component, so check first.
        if (find(y*w + x, components) != find((y-1)*w + x, components))
          union_(curr_component[x], prev_component[x], components, sz);
      }

      unsigned cur = find(y*w + x, components);
      if (sz[cur] > max_component_size) {
        max_component_size = sz[cur];
        max_component = cur;
      }
    }

    prev = curr;
    curr += w;

    prev_component = curr_component;
    curr_component += w;
  }
  free(sz);

  // Second pass: Wipe out everything that isn't in the biggest class.
  for (unsigned i = 0; i < n; ++i)
    if (find(i, components) != max_component)
      graymap->data[i] = 255;
  free(components);
}
