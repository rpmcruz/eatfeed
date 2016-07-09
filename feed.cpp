// feed.cpp

#include "feed.h"
#include <gdk/gdk.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <fstream>
#include <iostream>

// in minutes
#define REFRESH_INTERVAL 30

// utilities

static std::string prefix_homedir (const char *dir)
{
	std::string str (getenv ("HOME"));
	return str + "/" + dir;
}

void replace (std::string &str, char dead, char live)
{
	// if we ever need to accept more characters make it accept a pair like
	// "13579" and "24680" to map '1' to '2' and so on.
	for (unsigned int i = 0; i < str.size(); i++)
		if (str[i] == dead)
			str[i] = live;
}

// News

News::News (Feed *feed)
: feed (feed), is_read (false)
{
}

void News::setRead (bool read)
{
	if (is_read != read) {
		is_read = read;
		feed->newsStatusChanged (this);
		if (read)
			feed->read_news.push_back (id);
		else
			feed->read_news.remove (id);
	}
}

static const std::string empty_str;

const std::string &News::updateDate() const
{ return _date == _updateDate ? empty_str : _updateDate; }

void News::setTitle (const std::string &str)
{ _title = str; }
void News::setSummary (const std::string &str)  // content overloads summary
{ if (_summary.empty()) _summary = str; }
void News::setContent (const std::string &str)
{ _summary = str; }
void News::setLink (const std::string &str)
{ _link = str; }
void News::setDate (const std::string &str)
{ _date = str; }
void News::setUpdateDate (const std::string &date, const std::string &id)
{ _updateDate = date; updateId = id; }
void News::setAuthor (const std::string &str)
{ _author = str; }
void News::addCategory (const std::string &str)
{ if (!_categories.empty()) _categories += ", "; _categories += str; }
void News::setId (const std::string &str)
{
	id = str;  // check if already read
	if (std::find (feed->read_news.begin(), feed->read_news.end(), id) != feed->read_news.end())
		is_read = true;
	if (_link.empty() && str.compare (0, 7, "http://") == 0)
		_link = str;
}

// Feed

Feed::Feed (const std::string &_url, const std::string &title,
            const std::string &codeset)
: url (_url), _title (title), codeset (codeset), _loading (false), _iconPixbuf (NULL)
{
}

Feed::~Feed()
{
	clear();
	if (_iconPixbuf) g_object_unref (_iconPixbuf);
}

void Feed::clear()
{
	for (std::vector <News *>::iterator it = news.begin(); it != news.end(); it++)
		delete *it;
	news.clear();
	error_msg.clear();
}

News *Feed::getNews (int nb) const
{
	if (nb >= (signed) news.size())
		return NULL;
	return news[nb];
}

int Feed::getNewsNb (News *n) const
{
	for (unsigned int i = 0; i < news.size(); i++)
		if (news[i] == n)
			return i;
	return 0;
}

gpointer Feed::parse_thread_cb (gpointer data)
{
	Feed *pThis = (Feed *) data;
	std::string error;
	parse (pThis, pThis->url, pThis->codeset, error);

	pThis->_loading = false;
	pThis->error_msg = error;
	if (error.empty()) {
		// we don't want to keep stored the washed up old flags
		pThis->read_news.clear();
		for (std::vector <News *>::const_iterator it = pThis->news.begin();
		     it != pThis->news.end(); it++) {
			if ((*it)->isRead())
				pThis->read_news.push_back ((*it)->id);
		}
		// icon is not loaded concurrently because it requires link from xml
		if (!pThis->_iconPixbuf)
			pThis->loadIcon();
	}

	gdk_threads_enter();
	Manager::get()->feedLoaded (pThis);
	gdk_flush();
	gdk_threads_leave();
	return 0;
}

void Feed::refresh()
{
	if (!_loading) {
		_loading = true;
		error_msg.clear();
		Manager::get()->feedLoading (this);
		clear();

		GError *error;
		GThread *thread = g_thread_create_full (parse_thread_cb, this, 0, FALSE, FALSE,
			G_THREAD_PRIORITY_NORMAL, &error);
		if (!thread)
			printf ("Couldn't create thread for parser: %s\n", error->message);
	}
}

void Feed::newsStatusChanged (News *news)
{
	Manager::get()->feedStatusChanged (this);
}

int Feed::unreadNb() const
{
	int unread = 0;
	for (std::vector <News *>::const_iterator it = news.begin(); it != news.end(); it++)
		if (!(*it)->isRead())
			unread++;
	return unread;
}

void Feed::setUserTitle (const std::string &str)
{ _title = str; }

void Feed::setTitle (const std::string &str)
{ if (_title.empty()) _title = str; _oriTitle = str; }
void Feed::setDescription (const std::string &str)
{ _description = str; }
void Feed::setLink (const std::string &str)
{ _link = str; _icon = ""; }
void Feed::setAuthor (const std::string &str)
{ _author = str; }
void Feed::setLogo (const std::string &str)
{ _logo = str; }

void Feed::loadIcon()
{
	struct ParseFavicon : public XmlParser::Handler {
		static std::string get (const std::string &webpage)
		{
			std::string text, error;
			text = download (webpage, error);
			std::string::size_type i = 0;
			while ((i = text.find ("<link", i+1)) != std::string::npos) {
				std::string::size_type j = text.find ("rel=", i) + 5;
				if (text.compare (j, 4, "icon", 4) == 0 ||
				    text.compare (j, 13, "shortcut icon", 13) == 0) {
					std::string::size_type j = text.find ("href=", i);
					if (j != std::string::npos) {
						char quot = text[j+5];
						j += 6;
						std::string::size_type l = text.find (quot, j);
						return std::string (text, j, l-j);
					}
				}
			}
			return "";
		}
	};

	if (!_link.empty()) {
		std::string::size_type i = _link.find ('/', 7);
		std::string root (_link, 0, i);
		root += '/';

		_icon = ParseFavicon::get (_link);
		if (!_icon.empty()) {
			if (_icon.compare (0, 5, "http:", 5) != 0)
				_icon = root + _icon;
		}
		else
			_icon = root + "favicon.ico";
	}

	_iconPixbuf = loadPixbuf (_icon);
	if (_iconPixbuf) {
		if (gdk_pixbuf_get_width (_iconPixbuf) != 16 || gdk_pixbuf_get_height (_iconPixbuf) != 16) {
			GdkPixbuf *old = _iconPixbuf;
			_iconPixbuf = gdk_pixbuf_scale_simple (old, 16, 16, GDK_INTERP_BILINEAR);
			g_object_unref (G_OBJECT (old));
		}
	}
}

static size_t download_icon_cb (char *buffer, size_t size, size_t nitems, void *data)
{
	GdkPixbufLoader *loader = (GdkPixbufLoader *) data;
	if (gdk_pixbuf_loader_write (loader, (guchar *) buffer, size * nitems, NULL))
		return size * nitems;
	return 0;
}

GdkPixbuf *Feed::loadPixbuf (const std::string &url)
{
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
	download (url, download_icon_cb, loader);
	gdk_pixbuf_loader_close (loader, NULL);
	GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
	if (pixbuf)
		g_object_ref (G_OBJECT (pixbuf));
	g_object_unref (loader);
	return pixbuf;
}

ParseNewsHandler *Feed::appendNews()
{
	News *n = new News (this);
	news.push_back (n);
	return n;
}

XmlParser::Handler *Feed::startElement (const char *name,
	const char **attribute_names, const char **attribute_values,
	std::string &error)
{
	if (!strcmp (name, "news")) {
		const char *id = XmlParser::get_value ("id", attribute_names, attribute_values);
		if (id)
			read_news.push_back (id);
	}
	return NULL;
}

void Feed::saveConfig (std::ofstream &stream) const
{
	std::string _url (url);  // what the HELL?
	replace (_url, '&', '@');  // check Manager::startElement() for info

	stream << "\t<feed title=\"" << _title << "\" url=\"" << _url << "\"";
	if (!codeset.empty())
		stream << " codeset=\"" << codeset << "\"";
	stream << ">\n";
	// if news empty, then the site is down or the network is; save previous info
	for (std::list <std::string>::const_iterator it = read_news.begin();
	     it != read_news.end(); it++)
		stream << "\t\t<news id=\"" << *it << "\"></news>\n";
	stream << "\t</feed>\n";
}

// Manager

Manager::Manager()
: feeds_loading (0), feeds_loaded (0)
{
	loadConfig();
	g_timeout_add_seconds_full (G_PRIORITY_LOW, REFRESH_INTERVAL*60,
	                            refresh_timeout, this, NULL);
}

Manager *Manager::get()
{
	static Manager *singleton = 0;
	if (!singleton) singleton = new Manager();
	return singleton;
}

gboolean Manager::refresh_timeout (gpointer data)
{
	Manager *pThis = (Manager *) data;
	pThis->saveConfig();  // save sometimes to subsidize a crash
	pThis->refreshAll();
	return TRUE;  // keep going
}

Feed *Manager::addFeed (const std::string &url, const std::string &title,
                        const std::string &codeset)
{
	std::string _url (url);
	if (!_url.compare (0, 7, "feed://")) {
		_url[0] = 'h'; _url[1] = 't'; _url[2] = 't'; _url[3] = 'p';
	}

	notifyStartStructuralChange();
	Feed *feed = new Feed (_url, title, codeset);
	feeds.push_back (feed);
	notifyEndStructuralChange();
	return feed;
}

void Manager::removeFeed (Feed *feed)
{
	notifyStartStructuralChange();
	std::vector <Feed *>::iterator it = std::find (feeds.begin(), feeds.end(), feed);
	if (it != feeds.end()) {
		feeds.erase (it);
		delete feed;
	}
	notifyEndStructuralChange();
}

#if 0
void Manager::swap (Feed *feed1, Feed *feed2)
{
	notifyStartStructuralChange();
	std::list <Feed *>::iterator it1 = std::find (feeds.begin(), feeds.end(), feed1);
	std::list <Feed *>::iterator it2 = std::find (feeds.begin(), feeds.end(), feed2);
	(*it1) = feed2;
	(*it2) = feed1;
	notifyEndStructuralChange();
}
#endif

void Manager::move (Feed *feed, int pos)
{
	notifyStartStructuralChange();
	std::vector <Feed *>::iterator it;
	int i;
	for (i = 0, it = feeds.begin(); it != feeds.end(); it++, i++)
		if (*it == feed) {
			feeds.erase (it);
			if (i < pos)
				pos--;
			break;
		}
	for (i = 0, it = feeds.begin(); it != feeds.end(); it++, i++)
		if (i == pos) {
			feeds.insert (it, feed);
			break;
		}
	if (it == feeds.end())
		feeds.push_back (feed);
	notifyEndStructuralChange();
}

void Manager::notifyStartStructuralChange()
{
	for (std::list <Listener *>::iterator it = listeners.begin(); it != listeners.end(); it++)
		(*it)->startStructuralChange (this);
}

void Manager::notifyEndStructuralChange()
{
	for (std::list <Listener *>::iterator it = listeners.begin(); it != listeners.end(); it++)
		(*it)->endStructuralChange (this);
}

Feed *Manager::getFeed (int nb) const
{
	if (nb >= (signed) feeds.size())
		return NULL;
	return feeds[nb];
}

int Manager::getFeedNb (Feed *feed) const
{
	for (unsigned int i = 0; i < feeds.size(); i++)
		if (feeds[i] == feed)
			return i;
	return 0;
}

void Manager::refreshAll()
{
	for (std::vector <Feed *>::iterator it = feeds.begin(); it != feeds.end(); it++)
		(*it)->refresh();
}

int Manager::unreadNb() const
{
	int unread = 0;
	for (std::vector <Feed *>::const_iterator it = feeds.begin(); it != feeds.end(); it++)
		unread += (*it)->unreadNb();
	return unread;
}

void Manager::feedStatusChanged (Feed *feed)
{
	for (std::list <Listener *>::iterator it = listeners.begin(); it != listeners.end(); it++)
		(*it)->feedStatusChange (this, feed);
}

void Manager::feedLoading (Feed *feed)
{
	feeds_loading++;
	for (std::list <Listener *>::iterator it = listeners.begin(); it != listeners.end(); it++) {
		(*it)->feedStatusChange (this, feed);
		(*it)->feedLoading (this, feed);
		if (feeds_loading > 1)
			(*it)->feedsLoadingProgress (this, ((float) feeds_loaded) / feeds_loading);
	}
}

void Manager::feedLoaded (Feed *feed)
{
	feeds_loaded++;
	for (std::list <Listener *>::iterator it = listeners.begin(); it != listeners.end(); it++) {
		(*it)->feedStatusChange (this, feed);
		(*it)->feedLoaded (this, feed);
		if (feeds_loading > 1)
			(*it)->feedsLoadingProgress (this, ((float) feeds_loaded) / feeds_loading);
	}
	if (feeds_loaded >= feeds_loading)
		feeds_loading = (feeds_loaded = 0);
}

void Manager::loadConfig()
{
	std::ifstream stream (prefix_homedir (".eatfeed").c_str());
	if (stream.good()) {
		XmlParser parser (this);
		std::string error;
		char buffer [4096];
		while (stream.getline (buffer, 4096)) {
			if (!parser.parse (buffer, error)) {
				std::cout << "Error: couldn't parse .eatfeed: " << error << std::endl;
				break;
			}
		}
	}
	else
		std::cout << "Error: couldn't open .eatfeed for reading.\n";
	atexit (saveManager);
}

XmlParser::Handler *Manager::startElement (const char *name,
	const char **attribute_names, const char **attribute_values,
	std::string &error)
{
	if (!strcmp (name, "feed")) {
		const char *title = "", *url = 0, *codeset = "";
		for (int i = 0; attribute_names[i]; i++) {
			if (!strcmp (attribute_names[i], "title"))
				title = attribute_values[i];
			else if (!strcmp (attribute_names[i], "url"))
				url = attribute_values[i];
			else if (!strcmp (attribute_names[i], "codeset"))
				codeset = attribute_values[i];
		}
		if (url) {
			// gtk xml parser has some adversity to chars on attributes like &
			// I thought of escaping the url at the Feed constrctor ('&' -> '%26')
			// but it seem as sites pass the url to scripts, some don't handle
			// that http escape code
			std::string _url (url);
			replace (_url, '@', '&');

			Feed *feed = addFeed (_url, title, codeset);
			return feed;
		}
	}
	return NULL;
}

void Manager::saveConfig() const
{
	std::ofstream stream (prefix_homedir (".eatfeed").c_str());
	if (stream.good()) {
		stream << "<eatfeed>\n";
		for (std::vector <Feed *>::const_iterator it = feeds.begin(); it != feeds.end(); it++)
			(*it)->saveConfig (stream);
		stream << "</eatfeed>\n";
	}
	else
		std::cout << "Error: couldn't open .eatfeed for saving.\n";
}

void Manager::saveManager()
{ Manager::get()->saveConfig(); }

