// This file is part of Snownews - A lightweight console RSS newsreader
//
// Copyright (c) 2003-2004 Oliver Feiler <kiza@kcore.de>
//
// Snownews is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3
// as published by the Free Software Foundation.
//
// Snownews is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Snownews. If not, see http://www.gnu.org/licenses/.

#include "ui-support.h"
#include <ncurses.h>
#include <unistd.h>

// Init the ncurses library.
void InitCurses (void) {
	initscr();
	keypad (stdscr, TRUE);	// Activate keypad so we can read function keys with getch.
	cbreak();		// No buffering.
	noecho();		// Do not echo typed chars onto the screen.
	clear();
	curs_set (_settings.cursor_always_visible);
}

// Print text in statusbar.
void UIStatus (const char* text, int delay, int warning) {
	int attr = WA_REVERSE;
	if (warning)
		attr |= COLOR_PAIR(10);
	attron (attr);
	clearLine (LINES-1, INVERSE);
	attron (WA_REVERSE);
	mvaddnstr (LINES-1, 1, text, COLS-2);
	attroff (attr);
	refresh();
	if (delay)
		sleep (delay);
}

// Swap all pointers inside a feed struct except next and prev
void SwapPointers (struct feed* one, struct feed* two) {
	struct feed *two_next = two->next, *two_prev = two->prev;
	struct feed tmp = *one;
	*one = *two;
	*two = tmp;
	one->next = two->next;
	one->prev = two->prev;
	two->next = two_next;
	two->prev = two_prev;
}

// Ignore "A", "The", etc. prefixes when sorting feeds.
const char* SnowSortIgnore (const char* title) {
	if (strncasecmp (title, "a ", strlen("a ")) == 0)
		return title+strlen("a ");
	else if (strncasecmp (title, "the ", strlen("the ")) == 0)
		return title+strlen("the ");
	return title;
}

// Sort the struct list alphabetically.
// Sorting criteria is struct feed->title
void SnowSort (void) {
	// If there is no element do not run sort or it'll crash!
	if (!_feed_list)
		return;
	UIStatus(_("Sorting, please wait..."), 0, 0);

	unsigned elements = 0;
	for (const struct feed* f = _feed_list; f; f = f->next)
		elements++;
	for (unsigned i = 0; i <= elements; ++i) {
		for (struct feed* f = _feed_list; f->next; f = f->next) {
			const char* one = SnowSortIgnore (f->title);
			const char* two = SnowSortIgnore (f->next->title);
			if (strcasecmp (one, two) > 0)
				SwapPointers (f, f->next);
		}
	}
}

// Draw a filled box with WA_REVERSE at coordinates x1y1/x2y2
void UISupportDrawBox (int x1, int y1, int x2, int y2) {
	attron (WA_REVERSE);
	for (int y = y1; y <= y2; ++y) {
		for (int x = x1; x <= x2; ++x) {
			int c = ' ';
			if (y == y1) {
				if (x == x1)
					c = ACS_ULCORNER;
				else if (x == x2)
					c = ACS_URCORNER;
				else
					c = ACS_HLINE;
			} else if (y == y2) {
				if (x == x1)
					c = ACS_LLCORNER;
				else if (x == x2)
					c = ACS_LRCORNER;
				else
					c = ACS_HLINE;
			} else if (x == x1 || x == x2)
				c = ACS_VLINE;
			mvaddch (y, x, c);
		}
	}
	attroff (WA_REVERSE);
}

// Draw main program header.
void UISupportDrawHeader (const char* headerstring) {
	clearLine (0, INVERSE);
	attron (WA_REVERSE);
	mvprintw (0, 1, "* Snownews %s  %s", SNOWNEWS_VERSTRING, headerstring ? headerstring : "");
	attroff (WA_REVERSE);
}

// Take a URL and execute in default browser.
// Apply all security restrictions before running system()!
void UISupportURLJump (const char* url) {
	if (!url)
		return;	// Should not happen. Nope, really should not happen.
	if (strncmp(url, "smartfeed", strlen("smartfeed")) == 0)
		return;	// Smartfeeds cannot be opened (for now).
	if (strchr (_settings.browser, '\'')) {
		UIStatus (_("Unsafe browser string (contains quotes)! See snownews.kcore.de/faq#toc2.4"), 5, 1);
		return;	// Complain loudly if browser string contains a single quote.
	}
	if (strchr (url, '\'')) {
		UIStatus (_("Invalid URL passed. Possible shell exploit attempted!"), 3, 1);
		return;
	}
	char escapedurl [PATH_MAX];
	snprintf (escapedurl, sizeof(escapedurl), "'%s' 2>/dev/null", url);
	char browcall [PATH_MAX];
	snprintf (browcall, sizeof(browcall), _settings.browser, escapedurl);

	char execmsg [128];
	snprintf (execmsg, sizeof(execmsg), _("Executing %s"), browcall);
	UIStatus (execmsg, 0, 0);

	endwin();
	system (browcall);
}

void SmartFeedsUpdate (void) {
	for (struct feed* f = _feed_list; f; f = f->next)
		if (f->smartfeed)
			SmartFeedNewitems (f);
}

void SmartFeedNewitems (struct feed* smart_feed) {
	// Be smart and don't leak the smart feed.
	// The items->data structures must not be freed, since a smart feed is only
	// a pointer collection and does not contain allocated memory.
	if (smart_feed->items) {
		while (smart_feed->items->next) {
			smart_feed->items = smart_feed->items->next;
			free (smart_feed->items->prev);
		}
		free (smart_feed->items);
		smart_feed->items = NULL;
	}
	for (struct feed* f = _feed_list; f; f = f->next) {
		// Do not add the smart feed recursively. 8)
		if (f == smart_feed)
			continue;
		for (struct newsitem* item = f->items; item; item = item->next) {
			// If item is unread, add to smart feed.
			if (item->data->readstatus)
				continue;
			struct newsitem* new_item = calloc (1, sizeof (struct newsitem));
			new_item->data = item->data;

			// Add to data structure.
			if (!smart_feed->items)
				smart_feed->items = new_item;
			else {
				new_item->prev = smart_feed->items;
				while (new_item->prev->next)
					new_item->prev = new_item->prev->next;
				new_item->prev->next = new_item;
			}
		}
	}
	// Only fill out once.
	if (!smart_feed->title)
		smart_feed->title = strdup (_("(New headlines)"));
	if (!smart_feed->link)
		smart_feed->link = strdup (smart_feed->feedurl);
}

bool SmartFeedExists (const char * smartfeed) {
	// Find our smart feed.
	if (strcmp (smartfeed, "newitems") == 0)
		for (const struct feed* f = _feed_list; f; f = f->next)
			if (f->smartfeed == 1)
				return true;
	return false;
}

void DrawProgressBar (int numobjects, int titlestrlen) {
	attron (WA_REVERSE);
	mvhline (LINES-1, titlestrlen+1, '=', numobjects);
	mvaddch (LINES-1, COLS-3, ']');
	refresh();
	attroff(WA_REVERSE);
}

void displayErrorLog (void) {
	erase();
	UISupportDrawHeader ("Error log");
	char errorfile [PATH_MAX];
	snprintf (errorfile, sizeof(errorfile), "%s/.snownews/error.log", getenv("HOME"));
	FILE* ef = fopen (errorfile, "r");
	if (ef) {
		char linebuf[128];
		move (1, 0);
		while (fgets (linebuf, sizeof(linebuf), ef))
			addstr (linebuf);
		fclose (ef);
	}
	getch();
}

void clearLine (int line, clear_line how) {
	if (how == INVERSE)
		attron(WA_REVERSE);
	mvhline (line, 0, ' ', COLS);
	if (how == INVERSE)
		attron(WA_REVERSE);
}
