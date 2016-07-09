// parser.h
// merges different feed formats into a generic feed handler

#ifndef PARSER_H
#define PARSER_H

#include <string>

struct ParseNewsHandler
{
	virtual void setTitle (const std::string &title) = 0;
	virtual void setSummary (const std::string &summary) = 0;
	virtual void setContent (const std::string &content) = 0;
	virtual void setLink (const std::string &link) = 0;
	virtual void setDate (const std::string &date) = 0;
	virtual void setUpdateDate (const std::string &date, const std::string &id) = 0;
	virtual void setAuthor (const std::string &author) = 0;
	virtual void addCategory (const std::string &category) = 0;
	virtual void setId (const std::string &id) = 0;
};

struct ParseFeedHandler
{
	virtual void setTitle (const std::string &title) = 0;
	virtual void setDescription (const std::string &description) = 0;
	virtual void setLink (const std::string &link) = 0;
	virtual void setAuthor (const std::string &author) = 0;
	virtual void setLogo (const std::string &logo) = 0;
	virtual ParseNewsHandler *appendNews() = 0;
};

void parse (ParseFeedHandler *handler, const std::string &url,
            const std::string &codeset, std::string &error_msg);

#endif /*PARSER_H*/

