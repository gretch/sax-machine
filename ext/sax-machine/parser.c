#include <search.h>
#include <string.h>
#include <stdio.h>
#include <ruby.h>
#include <stdlib.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xmlreader.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>

#define SAX_HASH_SIZE 200
#define MAX_TAGS 20
#define false 0
#define true 1

typedef struct {
	const char *setter;
	const char *value;
	const char **attrs;
} SAXMachineElement;

typedef struct {
	const char *name;
	int numberOfElements;
	SAXMachineElement *elements[MAX_TAGS];
} SAXMachineTag;

typedef struct saxMachineHandler SAXMachineHandler;
struct saxMachineHandler {
	short parseCurrentTag;
	SAXMachineTag *tags[SAX_HASH_SIZE];
	SAXMachineHandler *childHandlers[SAX_HASH_SIZE];
};

SAXMachineHandler *saxHandlersForClasses[SAX_HASH_SIZE];
SAXMachineHandler *handlerStack[20];
SAXMachineHandler *currentHandler;
int handlerStackTop;

const char * saxMachineTag;

// hash algorithm from R. Sedgwick, Algorithms in C++
static inline int hash_index(const char * key) {
	int h = 0, a = 127, temp;
	
	for (; *key != 0; key++) {
		temp = (a * h + *key);
		if (temp < 0) temp = -temp;
		h = temp % SAX_HASH_SIZE;
	}
	
	return h;
}

static SAXMachineHandler *new_handler() {
	SAXMachineHandler *handler = (SAXMachineHandler *) malloc(sizeof(SAXMachineHandler));
	handler->parseCurrentTag = false;
	int i;
	for (i = 0; i < SAX_HASH_SIZE; i++) {
		handler->tags[i] = NULL;
		handler->childHandlers[i] = NULL;
	}
	return handler;
}

static SAXMachineElement * new_element() {
	SAXMachineElement * element = (SAXMachineElement *) malloc(sizeof(SAXMachineElement));
	element->setter = NULL;
	element->value = NULL;
	return element;	
}

static SAXMachineTag * new_tag(const char * name) {
	SAXMachineTag * tag = (SAXMachineTag *) malloc(sizeof(SAXMachineTag));
	int i;
	for (i = 0; i < MAX_TAGS; i++) {
		tag->elements[i] = NULL;
	}
	tag->numberOfElements = 0;
	tag->name = name;
	return tag;
}

static inline SAXMachineHandler * handler_for_class(const char *name) {
	return saxHandlersForClasses[hash_index(name)];
}

static VALUE add_element(VALUE self, VALUE name, VALUE setter) {
	// first create the sax handler for this class if it doesn't exist
	VALUE klass = rb_funcall(rb_funcall(self, rb_intern("class"), 0), rb_intern("to_s"), 0);
	const char *className = StringValuePtr(klass);
	int handlerIndex = hash_index(className);
	if (saxHandlersForClasses[handlerIndex] == NULL) {
		saxHandlersForClasses[handlerIndex] = new_handler();
	}
	SAXMachineHandler *handler = saxHandlersForClasses[handlerIndex];
	
	// now create the tag if it's not there yet
	const char *tag_name = StringValuePtr(name);
	int tag_index = hash_index(tag_name);
	if (handler->tags[tag_index] == NULL) {
		handler->tags[tag_index] = new_tag(tag_name);
	}
	
	SAXMachineTag *tag = handler->tags[tag_index];
	
	// now create the element and add it to the tag
	SAXMachineElement * element = new_element();
	element->setter = StringValuePtr(setter);
	tag->elements[tag->numberOfElements++] = element;
	return name;
}

static inline SAXMachineHandler * currentHandlerParent() {
	if (handlerStackTop <= 0) {
		return NULL;
	}
	else {
		return handlerStack[handlerStackTop - 1];
	}
}

static inline short tag_matches_element_in_handler(SAXMachineHandler *handler, const xmlChar *name, const xmlChar **atts) {
	// here's a string compare example
	// strcmp((const char *)name, saxMachineTag) == 0
	int i = hash_index((const char *)name);
	if (handler->tags[i] != NULL && strcmp(handler->tags[i]->name, name) == 0) {
		return true;		
	}
	else {
		return false;
	}
}

static inline short tag_matches_child_handler_in_handler(SAXMachineHandler *handler, const xmlChar *name) {
	return handler->childHandlers[hash_index((const char *)name)] != NULL;
}

// static inline short tag_is_of_interest(SAXHandler * handler, const xmlChar *name, const xmlChar **atts) {
// 	// first see if it's an element that matches
// }

/*
 * call-seq:
 *  parse_memory(data)
 *
 * Parse the document stored in +data+
 */
static VALUE parse_memory(VALUE self, VALUE data)
{
  xmlSAXHandlerPtr handler;
  Data_Get_Struct(self, xmlSAXHandler, handler);
  xmlSAXUserParseMemory(  handler,
                          (void *)self,
                          StringValuePtr(data),
                          NUM2INT(rb_funcall(data, rb_intern("length"), 0))
  );
  return data;
}

static void start_document(void * ctx)
{
  VALUE self = (VALUE)ctx;
	VALUE klass = rb_funcall(rb_funcall(self, rb_intern("class"), 0), rb_intern("to_s"), 0);
	const char * className = StringValuePtr(klass);
	handlerStackTop = 0;
	handlerStack[handlerStackTop] = handler_for_class(className);
	currentHandler = handlerStack[handlerStackTop];
//  rb_funcall(self, rb_intern("start_document"), 0);
}

static void end_document(void * ctx)
{
	handlerStack[0] = NULL;
//  VALUE self = (VALUE)ctx;
//  rb_funcall(self, rb_intern("end_document"), 0);
}

static void start_element(void * ctx, const xmlChar *name, const xmlChar **atts)
{
	if (tag_matches_element_in_handler(currentHandler, name, atts)) {
		currentHandler->parseCurrentTag = true;
		
	  VALUE self = (VALUE)ctx;
	  VALUE attributes = rb_ary_new();
	  const xmlChar * attr;
	  int i = 0;
	  if(atts) {
	    while((attr = atts[i]) != NULL) {
	      rb_funcall(attributes, rb_intern("<<"), 1, rb_str_new2((const char *)attr));
	      i++;
	    }
	  }

	  rb_funcall( self,
	              rb_intern("start_element"),
	              2,
	              rb_str_new2((const char *)name),
	              attributes
	  );
	}
}

static void end_element(void * ctx, const xmlChar *name)
{
	if (currentHandler->parseCurrentTag) {
		currentHandler->parseCurrentTag = false;
		
	  VALUE self = (VALUE)ctx;
		const char * cname = (const char *)name;
	  rb_funcall(self, rb_intern("end_element"), 1, rb_str_new2(cname));
		
		// pop the stack if this is the end of a collection
		SAXMachineHandler * parent = currentHandlerParent();
		if (parent != NULL) {
			if (tag_matches_child_handler_in_handler(parent, name)) {
				handlerStack[handlerStackTop--] = NULL;
			}
		}
	}
}

static void characters_func(void * ctx, const xmlChar * ch, int len)
{
	if (currentHandler->parseCurrentTag) {
	  VALUE self = (VALUE)ctx;
	  VALUE str = rb_str_new((const char *)ch, (long)len);
	  rb_funcall(self, rb_intern("characters"), 1, str);
	}
}

static void comment_func(void * ctx, const xmlChar * value)
{
	if (currentHandler->parseCurrentTag) {
	  VALUE self = (VALUE)ctx;
	  VALUE str = rb_str_new2((const char *)value);
	  rb_funcall(self, rb_intern("comment"), 1, str);
	}
}

#ifndef XP_WIN
static void warning_func(void * ctx, const char *msg, ...)
{
  VALUE self = (VALUE)ctx;
  char * message;

  va_list args;
  va_start(args, msg);
  vasprintf(&message, msg, args);
  va_end(args);

  rb_funcall(self, rb_intern("warning"), 1, rb_str_new2(message));
  free(message);
}
#endif

#ifndef XP_WIN
static void error_func(void * ctx, const char *msg, ...)
{
  VALUE self = (VALUE)ctx;
  char * message;

  va_list args;
  va_start(args, msg);
  vasprintf(&message, msg, args);
  va_end(args);

  rb_funcall(self, rb_intern("error"), 1, rb_str_new2(message));
  free(message);
}
#endif

static void cdata_block(void * ctx, const xmlChar * value, int len)
{
	if (currentHandler->parseCurrentTag) {
	  VALUE self = (VALUE)ctx;
	  VALUE string = rb_str_new((const char *)value, (long)len);
	  rb_funcall(self, rb_intern("cdata_block"), 1, string);
	}
}

static void deallocate(xmlSAXHandlerPtr handler)
{
  free(handler);
}

static VALUE allocate(VALUE klass)
{
  xmlSAXHandlerPtr handler = calloc(1, sizeof(xmlSAXHandler));

  handler->startDocument = start_document;
  handler->endDocument = end_document;
  handler->startElement = start_element;
  handler->endElement = end_element;
  handler->characters = characters_func;
  handler->comment = comment_func;
#ifndef XP_WIN
  /*
   * The va*functions aren't in ming, and I don't want to deal with
   * it right now.....
   *
   */
  handler->warning = warning_func;
  handler->error = error_func;
#endif
  handler->cdataBlock = cdata_block;

  return Data_Wrap_Struct(klass, NULL, deallocate, handler);
}

static VALUE add_tag(VALUE self, VALUE tagName) {
	saxMachineTag = StringValuePtr(tagName);
	return tagName;
}

static VALUE get_cl(VALUE self) {
	return rb_funcall(self, rb_intern("class"), 0);
}

VALUE cNokogiriXmlSaxParser ;
void Init_native()
{
	// we're storing the sax handler information for all the classes loaded. null it out to start
	int i;
	for (i = 0; i < SAX_HASH_SIZE; i++) {
		saxHandlersForClasses[i] = NULL;
	}
	
  VALUE mSAXMachine = rb_const_get(rb_cObject, rb_intern("SAXMachine"));
  VALUE klass = cNokogiriXmlSaxParser =
    rb_const_get(mSAXMachine, rb_intern("Parser"));
  rb_define_alloc_func(klass, allocate);
  rb_define_method(klass, "parse_memory", parse_memory, 1);
  rb_define_method(klass, "add_tag", add_tag, 1);
	rb_define_method(klass, "get_cl", get_cl, 0);
	rb_define_method(klass, "add_element", add_element, 2);
}
