// cl /nologo xmltest.cc
// msxml6.tlh autolinks ole32.lib oleaut32.lib comsuppw.lib
#import <msxml6.dll>  // Generates and includes msxml6.tlh msxml6.tli
#include <stdio.h>

// Note: MSXML2::IFooPtr is different from ::IFooPtr.
// The MSXML2 types use exceptions instead of HRESULT.

void print(MSXML2::IXMLDOMNodePtr n, int indent) {
  variant_t node_value = n->nodeValue;
  bstr_t node_value_str;
  if (node_value.GetVARIANT().vt != VT_NULL)
    node_value_str = (bstr_t)node_value;
  for (int i = 0; i < indent; ++i) printf(" ");
  printf("%d/%S %S %S %S\n", n->nodeType, (BSTR)n->nodeTypeString,
         (BSTR)n->nodeName, (BSTR)node_value_str, (BSTR)n->namespaceURI);

  if (MSXML2::IXMLDOMNamedNodeMapPtr attribs = n->attributes)
    for (long i = 0; i < attribs->length; ++i)
      print(attribs->item[i], indent + 2);

  MSXML2::IXMLDOMNodeListPtr children = n->childNodes;
  for (long i = 0; i < children->length; ++i)
    print(children->item[i], indent + 2);
}

void DumpFile(const char* filename) {
  MSXML2::IXMLDOMDocumentPtr xml_doc;
  xml_doc.CreateInstance(__uuidof(MSXML2::DOMDocument60));
  xml_doc->async = VARIANT_FALSE;
  xml_doc->validateOnParse = VARIANT_FALSE;
  xml_doc->resolveExternals = VARIANT_FALSE;
  if (xml_doc->load(filename) != VARIANT_TRUE) {
    // Failed to load xml, get last parsing error
    printf("Failed to load DOM from stocks.xml. %S\n",
           (BSTR)xml_doc->parseError->reason);
    return;
  }
  print(xml_doc, 0);
}

int main(int argc, char *argv[]) {
  CoInitialize(NULL);
  for (int i = 1; i < argc; ++i)
    DumpFile(argv[i]);
  CoUninitialize();
}
