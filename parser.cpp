// parser.cpp

#include "parser.h"
#include "xmlparser.h"
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// date-time

static const char *months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static std::string format_date (int year, int month, int day, int hour, int min, int hour_zone, int min_zone)
{
	gchar *str;
	if (min_zone < 0)
		str = g_strdup_printf ("%s %2d, %d (%02d:%02d ----)",
			months[month-1], day, year, hour, min);
	else
		str = g_strdup_printf ("%s %2d, %d (%02d:%02d %c%02d:%02d)",
			months[month-1], day, year, hour, min, hour_zone < 0 ? '-' : '+', abs (hour_zone), min_zone);
	std::string s (str);
	g_free (str);
	return s;
}

static std::string parse_rfc3339 (const std::string &text)  // Atom
{
	// spec: http://tools.ietf.org/html/rfc3339#section-5.6
	if (text.size() < 20)
		return "";
	const char *str = text.c_str();
	int year, month, day, hour, min, hour_zone, min_zone;
	if (sscanf (str, "%4d-%2d-%2dT%2d:%2d", &year, &month, &day, &hour, &min) == 5) {
		str = str + 19;
		if (*str == '.')  // time-secfrac
			do { ++str;
			} while (*str != 'Z' && *str != '+' && *str != '-' && *str != '\0');
		if (*str == '\0')
			return "";
		if (*str == 'Z')
			hour_zone = min_zone = 0;
		else {
			sscanf (str+1, "%2d:%2d", &hour_zone, &min_zone);
			if (*str == '-')
				hour_zone = -hour_zone;
		}
		return format_date (year, month, day, hour, min, hour_zone, min_zone);
	}
	return "";
}

struct Timezone {
	const char *timezone;
	int hour;
};
Timezone timezones[] = {
	{ "GMT",  0 }, { "CDT", -5 }, { "CST", -6 }, { "EDT", -4 }, { "EST", -5 },
	{ "PDT", -7 }, { "PST", -8 },
};
static bool timezoneHour (const char *timezone, int *hour)
{
	for (unsigned int i = 0; i < sizeof (timezones) / sizeof (Timezone); i++)
		if (!strncmp (timezone, timezones[i].timezone, 3)) {
			*hour = timezones[i].hour;
			return true;
		}
	return false;
}

static std::string parse_rfc822 (const std::string &text)  // RSS
{
	// spec: http://asg.web.cmu.edu/rfc/rfc822.html
	if (text.size() < 26)
		return "";
	int year, month, day, hour, min, hour_zone, min_zone;
	const char *str = text.c_str();
	str += 5;
	day = atoi (str);
	if (isdigit (str[1]))
		str++;
	str += 2;
	for (month = 0; month < 12; month++)
		if (!strncasecmp (str, months[month], 3))
			break;
	if (month == 12)
		return "";
	month += 1;
	str += 4;
	year = atoi (str);
	str += 5;
	hour = atoi (str);
	str += 3;
	min = atoi (str);
	str += 6;
	if (g_ascii_isdigit (str[1])) {
		sscanf (str+1, "%2d%2d", &hour_zone, &min_zone);
		if (*str == '-')
			hour_zone = -hour_zone;
	}
	else if (timezoneHour (str, &hour_zone))
		min_zone = 0;
	else
		min_zone = -1;
	return format_date (year, month, day, hour, min, hour_zone, min_zone);
}

//** RSS

struct RssItemParser : public XmlParser::Handler  // <item>
{
	RssItemParser (ParseNewsHandler *handler)
	: handler (handler) {}

private:
	ParseNewsHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error) { return NULL; }

	virtual void textElement (const char *name, const std::string &text, std::string &error)
	{
		if (!strcmp (name, "title"))
			handler->setTitle (text);
		else if (!strcmp (name, "link"))
			handler->setLink (text);
		else if (!strcmp (name, "content") || !strcmp (name, "content:encoded"))
			handler->setContent (text);
		else if (!strcmp (name, "description") || !strcmp (name, "summary") || !strcmp (name, "atom:summary"))
			handler->setSummary (text);
		else if (!strcmp (name, "pubDate")) {
			std::string date (parse_rfc822 (text));
			handler->setDate (date);
		}
		else if (!strcmp (name, "author") || !strcmp (name, "dc:creator"))
			handler->setAuthor (text);
		else if (!strcmp (name, "category"))
			handler->addCategory (text);
		else if (!strcmp (name, "guid"))
			handler->setId (text);
	}

	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error)
	{ delete child; }
};

struct RssChannelImageParser : public XmlParser::Handler  // <channel><image>
{
	RssChannelImageParser (ParseFeedHandler *handler)
	: handler (handler) {}

private:
	ParseFeedHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error)
	{ return NULL; }

	virtual void textElement (const char *name, const std::string &text, std::string &error)
	{
		if (!strcmp (name, "url"))
			handler->setLogo (text);
	}

	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error)
	{ delete child; }
};

struct RssChannelParser : public XmlParser::Handler  // <channel>
{
	RssChannelParser (ParseFeedHandler *handler)
	: handler (handler) {}

private:
	ParseFeedHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error)
	{
		if (!strcmp (name, "item")) {
			ParseNewsHandler *newsHandler = handler->appendNews();
			return new RssItemParser (newsHandler);
		}
		if (!strcmp (name, "image"))
			return new RssChannelImageParser (handler);
		return NULL;
	}

	virtual void textElement (const char *name, const std::string &text, std::string &error)
	{
		if (!strcmp (name, "title"))
			handler->setTitle (text);
		else if (!strcmp (name, "link"))
			handler->setLink (text);
		else if (!strcmp (name, "description"))
			handler->setDescription (text);
		else if (!strcmp (name, "managingEditor"))
			handler->setAuthor (text);
	}

	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error)
	{ delete child; }
};

struct RssParser : public XmlParser::Handler  // <rss>
{
	RssParser (ParseFeedHandler *handler)
	: handler (handler) {}

private:
	ParseFeedHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error)
	{
		if (!strcmp (name, "channel"))
			return new RssChannelParser (handler);
		return NULL;
	}

	virtual void textElement (const char *name, const std::string &text, std::string &error)
	{
		if (!strcmp (name, "title"))
			handler->setTitle (text);
		else if (!strcmp (name, "link"))
			handler->setLink (text);
		else if (!strcmp (name, "description"))
			handler->setDescription (text);
	}

	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error)
	{ delete child; }
};

//** Atom

struct AtomEntryAuthorParser : public XmlParser::Handler  // <author>
{
	AtomEntryAuthorParser (ParseNewsHandler *handler)
	: handler (handler) {}

private:
	ParseNewsHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error)
	{ return NULL; }

	virtual void textElement (const char *name, const std::string &text, std::string &error)
	{
		if (!strcmp (name, "name"))
			handler->setAuthor (text);
	}

	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error) {}
};

struct AtomEntryParser : public XmlParser::Handler  // <entry>
{
	AtomEntryParser (ParseNewsHandler *handler)
	: handler (handler) {}

private:
	ParseNewsHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error)
	{
		if (!strcmp (name, "link")) {
			const char *type = XmlParser::get_value ("type",
				attribute_names, attribute_values);
			const char *href = XmlParser::get_value ("href",
				attribute_names, attribute_values);
			if (href && (!type || !strcmp (type, "text/html")))
				handler->setLink (href);
		}
		else if (!strcmp (name, "author"))
			return new AtomEntryAuthorParser (handler);
		return NULL;
	}

	virtual void textElement (const char *name, const std::string &text, std::string &error)
	{
		if (!strcmp (name, "title"))
			handler->setTitle (text);
		else if (!strcmp (name, "summary"))
			handler->setSummary (text);
		else if (!strcmp (name, "content"))
			handler->setContent (text);
		else if (!strcmp (name, "created") || !strcmp (name, "published")) {
			std::string date (parse_rfc3339 (text));
			handler->setDate (date);
		}
		else if (!strcmp (name, "updated")) {
			std::string date (parse_rfc3339 (text));
			handler->setUpdateDate (date, text);
		}
		else if (!strcmp (name, "category") || !strcmp (name, "dc:subject"))
			handler->addCategory (text);
		else if (!strcmp (name, "id"))
			handler->setId (text);
	}

	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error)
	{ delete child; }
};

struct AtomAuthorParser : public XmlParser::Handler  // <author>
{
	AtomAuthorParser (ParseFeedHandler *handler)
	: handler (handler) {}

private:
	ParseFeedHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error)
	{ return NULL; }

	virtual void textElement (const char *name, const std::string &text, std::string &error)
	{
		if (!strcmp (name, "name"))
			handler->setAuthor (text);
	}

	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error) {}
};

struct AtomFeedParser : public XmlParser::Handler  // <feed>
{
	AtomFeedParser (ParseFeedHandler *handler)
	: handler (handler) {}

private:
	ParseFeedHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error)
	{
		if (!strcmp (name, "entry"))
			return new AtomEntryParser (handler->appendNews());
		else if (!strcmp (name, "author"))
			return new AtomAuthorParser (handler);
		else if (!strcmp (name, "link")) {
			const char *type = XmlParser::get_value ("type",
				attribute_names, attribute_values);
			const char *href = XmlParser::get_value ("href",
				attribute_names, attribute_values);
			if (href && (!type || !strcmp (type, "text/html")))
				handler->setLink (href);
		}
		return NULL;
	}

	virtual void textElement (const char *name, const std::string &text, std::string &error)
	{
		if (!strcmp (name, "title"))
			handler->setTitle (text);
		else if (!strcmp (name, "subtitle"))
			handler->setDescription (text);
	}

	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error)
	{ delete child; }
};

//** top XML block

struct TopParser : public XmlParser::Handler  // <rss>
{
	TopParser (ParseFeedHandler *handler)
	: handler (handler) {}

private:
	ParseFeedHandler *handler;

	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error)
	{
		if (!strcmp (name, "rss")) {
			const char *version = XmlParser::get_value ("version",
				attribute_names, attribute_values);
			if (!strcmp (version, "1.0"))
				error = std::string ("Unsupported RSS version: ") + version;
			else
				return new RssParser (handler);
		}
		else if (!strcmp (name, "feed"))
			return new AtomFeedParser (handler);
		else
			error = std::string ("Unsupported format: ") + name;
		return NULL;
	}

	virtual void textElement (const char *name, const std::string &text,
		std::string &error) {}
	virtual void endElement (const char *name, XmlParser::Handler *child, std::string &error)
	{ delete child; }
};

void parse (ParseFeedHandler *handler, const std::string &url,
            const std::string &codeset, std::string &error_msg)
{
	std::string text = download (url, error_msg);
	if (text.empty()) {
		if (error_msg.empty())
			error_msg = "Download failed";
		return;
	}
	if (!codeset.empty()) {
		GError *error = 0;
		gsize bytes_read, bytes_written;
		gchar *_text = g_convert (text.c_str(), -1, "utf8", codeset.c_str(),
		                          &bytes_read, &bytes_written, &error);
		if (!_text) {
			error_msg = error->message;
			g_error_free (error);
			return;
		}
		text = _text;
		g_free (_text);
	}

	TopParser _handler (handler);
	XmlParser parser (&_handler);
	parser.parse (text, error_msg);
}

