# Eatfeed
RSS/Atom news client to read blogs

Currently, I am using [Feedly](http://feedly.com/). At one time, there was a serious deficient in RSS/Atom Linux readers. This client was even featured in [OMG Ubuntu](http://www.omgubuntu.co.uk/2009/06/eatfeed-simple-desktop-rss-reader).

![screenshot](https://github.com/rpmcruz/tagmail/raw/master/eatfeed.png "Screenshot")

**Requirements:** C++ compiler (gcc) and GTK+2.

Also, it uses Webkit or LibGtkHtml for HTML rendering, whichever is installed.

It uses Glib for parsing the RSS/Atom XML, so no more libraries are necessary. Of course, not everybody follows standards closely. The parser already does some gymnastics to cope with that. But it may need to be tweaked to support other websites.

If you find this interest, [let me know](mailto:ricardo.pdm.cruz@gmail.com). It should be easy to port to more recent versions of GTK+. I have done so for other projects.

(C) 2009 Ricardo Cruz under the GPLv3
