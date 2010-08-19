/*
 Copyright (C) 1999-2006 Id Software, Inc. and contributors.
 For a list of contributors, see the accompanying CONTRIBUTORS file.

 This file is part of GtkRadiant.

 GtkRadiant is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 GtkRadiant is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with GtkRadiant; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "console.h"
#include "radiant_i18n.h"

#include <iostream>
#include <time.h>

#include "gtkutil/accelerator.h"
#include "gtkutil/messagebox.h"
#include "gtkutil/container.h"
#include "gtkutil/menu.h"
#include "gtkutil/nonmodal.h"
#include "stream/stringstream.h"
#include "convert.h"
#include "os/file.h"

#include "version.h"
#include "aboutmsg.h"
#include "gtkmisc.h"
#include "mainframe.h"

// handle to the console log file
namespace
{
	FILE* g_hLogFile;
}

bool g_Console_enableLogfile = false;

static bool g_Console_createLogFailed = false;

/**
 * @note called whenever we need to open/close/check the console log file
 */
void Sys_LogFile (bool enable)
{
	if (enable && !g_hLogFile) {
		// settings say we should be logging and we don't have a log file .. so create it
		if (SettingsPath_get().empty())
			return; // cannot open a log file yet
		// open a file to log the console (if user prefs say so)
		// the file handle is g_hLogFile
		// the log file is erased
		std::string name = SettingsPath_get() + "radiant.log";
		g_hLogFile = fopen(name.c_str(), "w");
		if (g_hLogFile != 0) {
			globalOutputStream() << "Started logging to " << name.c_str() << "\n";
			time_t localtime;
			time(&localtime);
			g_Console_createLogFailed = false;
			globalOutputStream() << "Today is: " << ctime(&localtime) << "\n";
			globalOutputStream()
					<< "This is UFORadiant '" RADIANT_VERSION "' compiled " __DATE__ "\n" RADIANT_ABOUTMSG "\n";
		} else {
			if (g_Console_createLogFailed)
				return;
			gtk_MessageBox(0, _("Failed to create log file, check write permissions in UFORadiant directory.\n"),
					_("Console logging"), eMB_OK, eMB_ICONERROR);
			g_Console_createLogFailed = true;
		}
	} else if (!enable && g_hLogFile != 0) {
		// settings say we should not be logging but still we have an active logfile .. close it
		time_t localtime;
		time(&localtime);
		g_message("Closing log file at %s\n", ctime(&localtime));
		fclose(g_hLogFile);
		g_hLogFile = NULL;
	}
}

static GtkWidget* g_console = NULL;

static void console_clear ()
{
	GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_console));
	gtk_text_buffer_set_text(buffer, "", -1);
}

static void console_populate_popup (GtkTextView* textview, GtkMenu* menu, gpointer user_data)
{
	menu_separator(menu);

	GtkWidget* item = gtk_menu_item_new_with_label("Clear");
	g_signal_connect(G_OBJECT (item), "activate", G_CALLBACK(console_clear), 0);
	gtk_widget_show(item);
	container_add_widget(GTK_CONTAINER(menu), item);
}

static gboolean destroy_set_null (GtkWindow* widget, GtkWidget** p)
{
	*p = NULL;
	return FALSE;
}

GtkWidget* Console_constructWindow (GtkWindow* toplevel)
{
	GtkWidget* scr = gtk_scrolled_window_new(0, 0);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scr), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scr), GTK_SHADOW_IN);
	gtk_widget_show(scr);

	{
		GtkWidget* text = gtk_text_view_new();
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
		gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
		gtk_container_add(GTK_CONTAINER (scr), text);
		gtk_widget_show(text);
		g_console = text;

		widget_connect_escape_clear_focus_widget(g_console);

		g_signal_connect(G_OBJECT(g_console), "populate-popup", G_CALLBACK(console_populate_popup), 0);
		g_signal_connect(G_OBJECT(g_console), "destroy", G_CALLBACK(destroy_set_null), &g_console);
	}

	gtk_container_set_focus_chain(GTK_CONTAINER(scr), NULL);

	return scr;
}

class GtkTextBufferOutputStream: public TextOutputStream
{
		GtkTextBuffer* textBuffer;
		GtkTextIter* iter;
		GtkTextTag* tag;
	public:
		GtkTextBufferOutputStream (GtkTextBuffer* textBuffer, GtkTextIter* iter, GtkTextTag* tag) :
			textBuffer(textBuffer), iter(iter), tag(tag)
		{
		}
		std::size_t write (const char* buffer, std::size_t length)
		{
			gtk_text_buffer_insert_with_tags(textBuffer, iter, buffer, gint(length), tag, (char const*) 0);
			return length;
		}
};

std::size_t Sys_Print (int level, const char* buf, std::size_t length)
{
	bool contains_newline = std::find(buf, buf + length, '\n') != buf + length;

	if (level == SYS_ERR || level == SYS_WRN) {
		Sys_LogFile(true);
		std::cerr << buf;
	} else {
		std::cout << buf;
	}

	if (g_hLogFile != 0) {
		file_write(buf, length, g_hLogFile);
		if (contains_newline) {
			fflush(g_hLogFile);
		}
	}

	if (level != SYS_NOCON) {
		if (g_console != 0) {
			GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_console));

			GtkTextIter iter;
			gtk_text_buffer_get_end_iter(buffer, &iter);

			const GdkColor yellow = { 0, 0xb0ff, 0xb0ff, 0x0000 };
			const GdkColor red = { 0, 0xffff, 0x0000, 0x0000 };
			const GdkColor black = { 0, 0x0000, 0x0000, 0x0000 };

			static GtkTextTag* error_tag = gtk_text_buffer_create_tag(buffer, "red_foreground", "foreground-gdk", &red,
					0);
			static GtkTextTag* warning_tag = gtk_text_buffer_create_tag(buffer, "yellow_foreground", "foreground-gdk",
					&yellow, 0);
			static GtkTextTag* standard_tag = gtk_text_buffer_create_tag(buffer, "black_foreground", "foreground-gdk",
					&black, 0);
			GtkTextTag* tag;
			switch (level) {
			case SYS_WRN:
				tag = warning_tag;
				break;
			case SYS_ERR:
				tag = error_tag;
				break;
			case SYS_STD:
			case SYS_VRB:
			default:
				tag = standard_tag;
				break;
			}

			{
				GtkTextBufferOutputStream textBuffer(buffer, &iter, tag);
				if (!globalCharacterSet().isUTF8()) {
					BufferedTextOutputStream<GtkTextBufferOutputStream> buffered(textBuffer);
					buffered << ConvertLocaleToUTF8(StringRange(buf, buf + length));
				} else {
					textBuffer << StringRange(buf, buf + length);
				}
			}
		}
	}
	return length;
}

class SysPrintOutputStream: public TextOutputStream
{
	public:
		std::size_t write (const char* buffer, std::size_t length)
		{
			return Sys_Print(SYS_STD, buffer, length);
		}
};

class SysPrintErrorStream: public TextOutputStream
{
	public:
		std::size_t write (const char* buffer, std::size_t length)
		{
			return Sys_Print(SYS_ERR, buffer, length);
		}
};

class SysPrintWarningStream: public TextOutputStream
{
	public:
		std::size_t write (const char* buffer, std::size_t length)
		{
			return Sys_Print(SYS_WRN, buffer, length);
		}
};

static SysPrintOutputStream g_outputStream;

TextOutputStream& getSysPrintOutputStream ()
{
	return g_outputStream;
}

static SysPrintErrorStream g_errorStream;

TextOutputStream& getSysPrintErrorStream ()
{
	return g_errorStream;
}

static SysPrintWarningStream g_warningStream;

TextOutputStream& getSysPrintWarningStream ()
{
	return g_warningStream;
}
