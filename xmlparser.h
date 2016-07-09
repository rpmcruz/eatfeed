// xmlparser.h
// A wrapper to abstract the xml library (currently using glib).
// The real motivation however was to add support for recursive xml parsing
// which is only present in glib 2.18 up versions.

#ifndef XML_PARSER_H
#define XML_PARSER_H

#include <string>

struct XmlParser {
	struct Handler {
		virtual Handler *startElement (const char *name,
			const char **attribute_names, const char **attribute_values,
			std::string &error) = 0;
		virtual void textElement (const char *name, const std::string &text,
			std::string &error) = 0;
		virtual void endElement (const char *name, Handler *child,
			std::string &error) = 0;

		// if you want recursive parsing, return some other hook on startElement()
		// (NULL otherwise, or 'this') and then the parent on child's endElement()
	};

	XmlParser (Handler *handler);
	~XmlParser();

	// you may break xml text into various calls
	bool parse (const std::string &text, std::string &error_msg);

	// utilities:
	static const char *get_value (const char *attribute_name,
		const char **attribute_names, const char **attribute_values);

	struct Impl;
	Impl *impl;
};

// curl wrapper:
typedef size_t (*write_callback) (char *buffer, size_t size, size_t nitems, void *data);
bool download (const std::string &url, write_callback func, void *data);

std::string download (const std::string &url, std::string &error_msg);  // convenience

#endif /*XML_PARSER_H*/

