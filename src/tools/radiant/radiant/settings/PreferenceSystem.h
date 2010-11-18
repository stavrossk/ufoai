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

#if !defined(INCLUDED_PREFERENCES_H)
#define INCLUDED_PREFERENCES_H

#include "../dialog.h"
#include <list>
#include <map>
#include "preferencesystem.h"
#include "iregistry.h"

class PrefPage : public PreferencesPage
{
		Dialog& m_dialog;
		GtkWidget* m_vbox;
	public:
		PrefPage (Dialog& dialog, GtkWidget* vbox) :
			m_dialog(dialog), m_vbox(vbox)
		{
		}
		GtkWidget* appendCheckBox (const char* name, const char* flag, bool& data)
		{
			return m_dialog.addCheckBox(m_vbox, name, flag, data);
		}
		GtkWidget* appendCheckBox (const char* name, const char* flag, const BoolImportCallback& importCallback,
				const BoolExportCallback& exportCallback)
		{
			return m_dialog.addCheckBox(m_vbox, name, flag, importCallback, exportCallback);
		}

		/* greebo: This adds a checkbox and connects it to an XMLRegistry key.
		 * @returns: the pointer to the created GtkWidget */
		GtkWidget* appendCheckBox (const std::string& name, const std::string& flag, const std::string& registryKey)
		{
			return m_dialog.addCheckBox(m_vbox, name, flag, registryKey);
		}

		/* greebo: Appends an entry field with <name> as caption which is connected to the given registryKey
		 */
		GtkWidget* appendEntry(const std::string& name, const std::string& registryKey) {
			return m_dialog.addEntry(m_vbox, name, registryKey);
		}

		/* greebo: greebo: This adds a horizontal slider to the internally referenced VBox and connects
		 * it to the given registryKey. */
		void appendSlider (const std::string& name, const std::string& registryKey, bool draw_value, double value,
				double lower, double upper, double step_increment, double page_increment, double page_size)
		{
			m_dialog.addSlider(m_vbox, name, registryKey, draw_value, value, lower, upper, step_increment,
					page_increment, page_size);
		}

		/* greebo: Use this to add a dropdown selection box with the given list of strings as captions. The value
		 * stored in the registryKey is used to determine the currently selected combobox item */
		void appendCombo(const std::string& name, const std::string& registryKey, const ComboBoxValueList& valueList)
		{
			m_dialog.addCombo(m_vbox, name, registryKey, valueList);
		}

		// greebo: Adds a PathEntry to choose files or directories (depending on the given boolean)
		GtkWidget* appendPathEntry(const std::string& name, const std::string& registryKey, bool browseDirectories) {
			return m_dialog.addPathEntry(m_vbox, name, registryKey, browseDirectories);
		}

		/* greebo: Appends an entry field with spinner buttons which retrieves its value from the given
		 * RegistryKey. The lower and upper values have to be passed as well.
		 */
		GtkWidget* appendSpinner(const std::string& name, const std::string& registryKey,
								 double lower, double upper, int fraction) {
			return m_dialog.addSpinner(m_vbox, name, registryKey, lower, upper, fraction);
		}

		void appendCombo (const char* name, StringArrayRange values, const IntImportCallback& importCallback,
				const IntExportCallback& exportCallback)
		{
			m_dialog.addCombo(m_vbox, name, values, importCallback, exportCallback);
		}
		void appendRadio (const char* name, StringArrayRange names, const IntImportCallback& importCallback,
				const IntExportCallback& exportCallback)
		{
			m_dialog.addRadio(m_vbox, name, names, importCallback, exportCallback);
		}
		void appendRadio (const char* name, int& data, StringArrayRange names)
		{
			m_dialog.addRadio(m_vbox, name, data, names);
		}
		void appendRadioIcons (const char* name, StringArrayRange icons, const IntImportCallback& importCallback,
				const IntExportCallback& exportCallback)
		{
			m_dialog.addRadioIcons(m_vbox, name, icons, importCallback, exportCallback);
		}
		GtkWidget* appendPathEntry (const char* name, bool browse_directory,
				const StringImportCallback& importCallback, const StringExportCallback& exportCallback)
		{
			return m_dialog.addPathEntry(m_vbox, name, browse_directory, importCallback, exportCallback);
		}
		GtkWidget* appendSpinner (const char* name, double value, double lower, double upper,
				const FloatImportCallback& importCallback, const FloatExportCallback& exportCallback)
		{
			return m_dialog.addSpinner(m_vbox, name, value, lower, upper, importCallback, exportCallback);
		}
};

typedef Callback1<PrefPage*> PreferencesPageCallback;
typedef Callback1<PreferenceGroup&> PreferenceGroupCallback;

void PreferencesDialog_addInterfacePage (const PreferenceGroupCallback& callback);
void PreferencesDialog_addSettingsPage (const PreferenceGroupCallback& callback);

void PreferencesDialog_restartRequired (const std::string& staticName);

template<typename Value>
class LatchedValue
{
	public:
		Value m_value;
		Value m_latched;
		const std::string m_description;

		LatchedValue (Value value, const std::string& description) :
			m_latched(value), m_description(description)
		{
		}
		void useLatched ()
		{
			m_value = m_latched;
		}
		void import (Value value)
		{
			m_latched = value;
			if (m_latched != m_value) {
				PreferencesDialog_restartRequired(m_description);
			}
		}
};

typedef LatchedValue<bool> LatchedBool;
typedef MemberCaller1<LatchedBool, bool, &LatchedBool::import> LatchedBoolImportCaller;

typedef LatchedValue<int> LatchedInt;
typedef MemberCaller1<LatchedInt, int, &LatchedInt::import> LatchedIntImportCaller;

class PreferenceTreeGroup;

class PrefsDlg: public Dialog
{
		typedef std::list<PreferenceConstructor*> PreferenceConstructorList;
		// The list of all the constructors that have to be called on dialog construction
		PreferenceConstructorList _constructors;

	public:

		GtkWidget *m_notebook;

		virtual ~PrefsDlg ()
		{
		}

		/*! Utility function for swapping notebook pages for tree list selections */
		void showPrefPage (GtkWidget* prefpage);

		// Add the given preference constructor to the internal list
		void addConstructor(PreferenceConstructor* constructor);

	protected:

		/*! Dialog API */
		GtkWindow* BuildDialog ();
		void PostModal (EMessageBoxReturn code);

	private:
		// greebo: calls the constructors to add the preference elements
		void callConstructors(PreferenceTreeGroup& preferenceGroup);
};

extern PrefsDlg g_Preferences;

struct preferences_globals_t
{
		// disabled all INI / registry read write .. used when shutting down after registry cleanup
		bool disable_ini;
		preferences_globals_t () :
			disable_ini(false)
		{
		}
};
extern preferences_globals_t g_preferences_globals;

typedef struct _GtkWindow GtkWindow;
void PreferencesDialog_constructWindow (GtkWindow* main_window);
void PreferencesDialog_destroyWindow ();

void PreferencesDialog_showDialog ();

void Preferences_Init ();

void Preferences_Load ();
void Preferences_Save ();

void Preferences_Reset ();

#endif
