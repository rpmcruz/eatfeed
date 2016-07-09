// feed.h
// storage

#ifndef FEED_H
#define FEED_H

#include "parser.h"
#include "xmlparser.h"
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <list>
#include <vector>
#include <string>

class Feed;
class FeedManager;

class News : public ParseNewsHandler
{
std::string _title, _summary, _link, _date, _updateDate, _author, _categories;
std::string id, updateId;
Feed *feed;
bool is_read;

public:
	explicit News (Feed *feed);

	bool isRead() const { return is_read; }
	void setRead (bool read);

	const std::string &title() const   { return _title; }
	const std::string &summary() const { return _summary; }
	const std::string &link() const    { return _link; }
	const std::string &date() const    { return _date; }
	const std::string &updateDate() const;
	const std::string &author() const  { return _author; }
	const std::string &categories() const  { return _categories; }

	const Feed *from() { return feed; }

private:
	virtual void setTitle (const std::string &title);
	virtual void setSummary (const std::string &summary);
	virtual void setContent (const std::string &content);
	virtual void setLink (const std::string &link);
	virtual void setDate (const std::string &date);
	virtual void setUpdateDate (const std::string &date, const std::string &id);
	virtual void setAuthor (const std::string &author);
	virtual void addCategory (const std::string &category);
	virtual void setId (const std::string &id);
	friend class Feed;
};

class Feed : public ParseFeedHandler, XmlParser::Handler
{
std::string url, _title, _oriTitle, _description, _link, _author, _icon, _logo, codeset;
std::vector <News *> news;
std::list <std::string> read_news;
std::string error_msg;
bool _loading;
GdkPixbuf *_iconPixbuf;

public:
	explicit Feed (const std::string &url, const std::string &title,
	               const std::string &codeset);
	~Feed();

	const std::string &title() const       { return _title; }
	const std::string &oriTitle() const    { return _oriTitle; }
	const std::string &description() const { return _description; }
	const std::string &link() const        { return _link; }
	const std::string &author() const      { return _author; }
	const std::string &_url() const        { return url; }
	const GdkPixbuf *iconPixbuf() const    { return _iconPixbuf; }
	const std::string &logo() const        { return _logo; }

	bool loading() const { return _loading; }
	const std::string &errorMsg() const { return error_msg; }

	News *getNews (int nb) const;
	int getNewsNb (News *news) const;

	void refresh();
	int unreadNb() const;

	void setUserTitle (const std::string &title);

private:
	void clear();

	friend class News;
	friend class Manager;
	void newsStatusChanged (News *news);

	virtual void setTitle (const std::string &title);
	virtual void setDescription (const std::string &description);
	virtual void setLink (const std::string &link);
	virtual void setAuthor (const std::string &author);
	virtual void setLogo (const std::string &logo);
	void loadIcon();
	static GdkPixbuf *loadPixbuf (const std::string &url);
	virtual ParseNewsHandler *appendNews();

	static gpointer parse_thread_cb (gpointer data);
	static void parse_done_cb (ParseFeedHandler *handler, const std::string &error);

	// config
	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error);
	virtual void textElement (const char *name, const std::string &text,
		std::string &error) {}
	virtual void endElement (const char *name, XmlParser::Handler *child,
		std::string &error) {}
	void saveConfig (std::ofstream &stream) const;
};

class Manager : public XmlParser::Handler
{
public:
	struct Listener {
		virtual void feedStatusChange (Manager *manager, Feed *feed) = 0;
		virtual void feedLoading (Manager *manager, Feed *feed) = 0;
		virtual void feedLoaded (Manager *manager, Feed *feed) = 0;
		virtual void feedsLoadingProgress (Manager *manager, float fraction) = 0;

		// hack: to avoid telling Gtk exactly what rows were added/removed
		// just tell the view reload the model
		virtual void startStructuralChange (Manager *manager) = 0;
		virtual void endStructuralChange (Manager *manager) = 0;
	};
	void addListener (Listener *listener) { listeners.push_back (listener); }
	void removeListener (Listener *listener) { listeners.remove (listener); }

private:
	std::vector <Feed *> feeds;
	std::list <Listener *> listeners;

public:
	explicit Manager();
	static Manager *get();

	Feed *addFeed (const std::string &url, const std::string &title,
	               const std::string &codeset = "");
	void removeFeed (Feed *feed);
	void move (Feed *feed, int new_pos);
	void refreshAll();

	int unreadNb() const;

	Feed *getFeed (int nb) const;
	int getFeedNb (Feed *feed) const;

private:
	friend class Feed;
	void feedStatusChanged (Feed *feed);
	void feedLoading (Feed *feed);
	void feedLoaded (Feed *feed);
	int feeds_loading, feeds_loaded;

	void notifyStartStructuralChange();
	void notifyEndStructuralChange();

	static gboolean refresh_timeout (gpointer pData);

	// config
	void loadConfig();
	virtual XmlParser::Handler *startElement (const char *name,
		const char **attribute_names, const char **attribute_values,
		std::string &error);
	virtual void textElement (const char *name, const std::string &text,
		std::string &error) {}
	virtual void endElement (const char *name, XmlParser::Handler *child,
		std::string &error) {}
	void saveConfig() const;
	static void saveManager();
};

#endif /*FEED_H*/

