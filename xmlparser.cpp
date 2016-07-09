// xmlparser.cpp

#include "xmlparser.h"
#include <glib.h>
#include <string.h>

static void check_error (const std::string &error_msg, GError **error)
{
	if (!error_msg.empty())
		g_set_error (error, NULL, G_MARKUP_ERROR_INVALID_CONTENT,
			"Parsing error: %s", error_msg.c_str());
}

static void parse_start_element (GMarkupParseContext *context,
	const gchar *element_name, const gchar **attribute_names,
	const gchar **attribute_values, gpointer data, GError **error);
static void parse_text (GMarkupParseContext *context,
	const gchar *text, gsize text_len, gpointer data, GError **error);
static void parse_end_element (GMarkupParseContext *context,
	const gchar *element_name, gpointer data, GError **error);

static const GMarkupParser parser =
{ parse_start_element, parse_end_element, parse_text, NULL, NULL };

struct XmlParser::Impl
{
	XmlParser::Handler *handler;
	GSList *handler_stack;
	GMarkupParseContext *context;
	std::string text, text_element_name;

	Impl (XmlParser::Handler *handler)
	: handler (handler)
	{
		handler_stack = g_slist_append (NULL, handler);
		context = g_markup_parse_context_new (&parser,
			G_MARKUP_TREAT_CDATA_AS_TEXT, this, NULL);
	}

	bool parse (const std::string &text, std::string &error_msg)
	{
		GError *error = 0;
		if (!g_markup_parse_context_parse (context, text.c_str(), -1, &error)) {
			error_msg = error->message;
			g_error_free (error);
			return false;
		}
		return true;
	}

	~Impl()
	{
		g_markup_parse_context_free (context);
		//g_assert (g_slist_length (handler_stack) == 1);
		g_slist_free (handler_stack);
	}

	// we queue parse_text() calls because glib breaks it unnecessarly
	void pushText (const char *element_name, const char *_text, int text_len, std::string &error_msg)
	{
		if (!text.empty() && (text_element_name != element_name))
			flushText (error_msg);
		text_element_name = element_name;

		std::string str (_text, text_len);
		// glib does replace entities but gets stuck on self-referenciated entities
		// e.g. glib converts "&amp;gt;" to "&gt;" -- stopping short of ">"
		std::string::size_type i = 0;
		while ((i = str.find ("&gt;", i)) != std::string::npos) {
			str[i] = '>';
			str.erase (i+1, 3);
			i++;
		}
		text += str;
	}
	void flushText (std::string &error_msg)
	{
		if (!text.empty()) {
			top()->textElement (text_element_name.c_str(), text, error_msg);
			text.clear();
		}
	}

	void push (XmlParser::Handler *handler)
	{ handler_stack = g_slist_prepend (handler_stack, handler); }

	XmlParser::Handler *top()
	{ return (XmlParser::Handler *) handler_stack->data; }

	XmlParser::Handler *pop()
	{
		XmlParser::Handler *handler = top();
		handler_stack = g_slist_delete_link (handler_stack, handler_stack);
		return handler;
	}
};

static void parse_start_element (GMarkupParseContext *context,
	const gchar *element_name, const gchar **attribute_names,
	const gchar **attribute_values, gpointer data, GError **error)
{
	std::string error_msg;
	XmlParser::Impl *parser = (XmlParser::Impl *) data;
	parser->flushText (error_msg);

	XmlParser::Handler *handler = parser->top(), *child;
	child = handler->startElement (element_name, attribute_names, attribute_values, error_msg);
	if (!child)	child = handler;
	parser->push (child);

	check_error (error_msg, error);
}

static void parse_text (GMarkupParseContext *context,
	const gchar *text, gsize text_len, gpointer data, GError **error)
{
	std::string error_msg;
	XmlParser::Impl *parser = (XmlParser::Impl *) data;

	const gchar *element_name = g_markup_parse_context_get_element (context);
	parser->pushText (element_name, text, text_len, error_msg);
}

static void parse_end_element (GMarkupParseContext *context,
	const gchar *element_name, gpointer data, GError **error)
{
	std::string error_msg;
	XmlParser::Impl *parser = (XmlParser::Impl *) data;
	parser->flushText (error_msg);

	XmlParser::Handler *child = parser->pop();
	XmlParser::Handler *handler = parser->top();
	if (child == handler) child = NULL;

	handler->endElement (element_name, child, error_msg);
	check_error (error_msg, error);
}

XmlParser::XmlParser (XmlParser::Handler *handler)
: impl (new Impl (handler)) {}

XmlParser::~XmlParser()
{ delete impl; }

bool XmlParser::parse (const std::string &text, std::string &error_msg)
{ return impl->parse (text, error_msg); }

const char *XmlParser::get_value (const char *attribute_name,
	const char **attribute_names, const char **attribute_values)
{
	for (int i = 0; attribute_names[i]; i++)
		if (!strcmp (attribute_names[i], attribute_name))
			return attribute_values[i];
	return NULL;
}

// utitlies:

#include <curl/curl.h>
static char curl_errorBuffer [CURL_ERROR_SIZE] = { 0 };

bool download (const std::string &url, write_callback func, void *data)
{
	curl_errorBuffer[0] = '\0';
	CURL *curl;
	CURLcode result;
	curl = curl_easy_init();
	if (curl) {
		std::string buffer;
		curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, curl_errorBuffer);
		curl_easy_setopt (curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt (curl, CURLOPT_HEADER, 0);
		curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);
		curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, func);
		curl_easy_setopt (curl, CURLOPT_WRITEDATA, data);
		// set timeout to 60 secs and disable signals on timeout
		curl_easy_setopt (curl, CURLOPT_TIMEOUT, 60);
		curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1);
		result = curl_easy_perform (curl);  // action
		curl_easy_cleanup(curl);  
		return result == CURLE_OK;
	}
	return false;
}

std::string download (const std::string &url, std::string &error_msg)
{
	struct inner {
		static size_t writer (char *data, size_t size, size_t nitems, void *user_data)
		{
			std::string *buffer = (std::string *) user_data;
			size_t len = size * nitems;
			buffer->append (data, len);
			return len;
		}
	};

	std::string buffer;
	if (download (url, inner::writer, &buffer))
		return buffer;
	error_msg = curl_errorBuffer;
	return "";
}

