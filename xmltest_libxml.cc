// clang -o xmltest xmltest_libxml.cc -I$(xcrun -show-sdk-path)/usr/include/libxml2 -lxml2
#include <libxml/xmlreader.h>

const char* TypeStr(xmlElementType v) {
  switch (v) {
#define X(x) case x: return #x
    X(XML_ELEMENT_NODE);
    X(XML_ATTRIBUTE_NODE);
    X(XML_TEXT_NODE);
    X(XML_CDATA_SECTION_NODE);
    X(XML_ENTITY_REF_NODE);
    X(XML_ENTITY_NODE);
    X(XML_PI_NODE);
    X(XML_COMMENT_NODE);
    X(XML_DOCUMENT_NODE);
    X(XML_DOCUMENT_TYPE_NODE);
    X(XML_DOCUMENT_FRAG_NODE);
    X(XML_NOTATION_NODE);
    X(XML_HTML_DOCUMENT_NODE);
    X(XML_DTD_NODE);
    X(XML_ELEMENT_DECL);
    X(XML_ATTRIBUTE_DECL);
    X(XML_ENTITY_DECL);
    X(XML_NAMESPACE_DECL);
    X(XML_XINCLUDE_START);
    X(XML_XINCLUDE_END);
    X(XML_DOCB_DOCUMENT_NODE);
#undef X
    default: return "unknown element type";
  }
}

void print(xmlNodePtr n, int indent) {
  for (int i = 0; i < indent; ++i) printf(" ");
  printf("%d/%s %s %s", n->type, TypeStr(n->type), n->name, n->content);
  if (n->type != XML_DOCUMENT_NODE) {
    for (xmlNs* ns = n->ns; ns; ns = ns->next)
      printf(" (%s:%s %s)", ns->prefix, ns->href, TypeStr(ns->type));
    printf("\n");
    if (n->type != XML_ATTRIBUTE_NODE) {
      if (n->nsDef) {
        for (int i = 0; i < indent + 2; ++i)
          printf(" ");
        printf("def ns:");
        for (xmlNs* ns = n->nsDef; ns; ns = ns->next)
          printf(" (%s:%s %s)", ns->prefix, ns->href, TypeStr(ns->type));
        printf("\n");
      }
      if (n->properties)
        for (xmlAttr* attr = n->properties; attr; attr = attr->next)
          print((xmlNodePtr)attr, indent + 2);
    }
  } else
    printf("\n");
  for (xmlNodePtr child = n->children; child; child = child->next)
    print(child, indent + 1);
}

void DumpFile(const char* filename) {
  xmlDocPtr doc = xmlParseFile(filename);
  if (!doc) {
    fprintf(stderr, "failed to parse\n");
    exit(1);
  }
  print((xmlNodePtr)doc, 0);
}


int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i)
    DumpFile(argv[i]);
}
