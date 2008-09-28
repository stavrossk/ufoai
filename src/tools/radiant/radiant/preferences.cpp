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

//
// User preferences
//
// Leonardo Zide (leo@lokigames.com)
//

#ifdef _WIN32
# define xmlFree free
#endif
#include "preferences.h"

#include "debugging/debugging.h"

#include <gtk/gtk.h>

#include "generic/callback.h"
#include "math/vector.h"
#include "string/string.h"
#include "stream/stringstream.h"
#include "os/file.h"
#include "os/path.h"
#include "os/dir.h"
#include "gtkutil/filechooser.h"
#include "gtkutil/messagebox.h"
#include "cmdlib.h"

#include "error.h"
#include "console.h"
#include "xywindow.h"
#include "mainframe.h"
#include "qe3.h"
#include "dialogs/texteditor.h"

void Interface_constructPreferences (PreferencesPage& page)
{
#ifdef WIN32
	page.appendCheckBox("", "Default Text Editor", g_TextEditor_useWin32Editor);
#else
	{
		GtkWidget* use_custom = page.appendCheckBox("Text Editor", "Custom", g_TextEditor_useCustomEditor);
		GtkWidget* custom_editor = page.appendPathEntry("Text Editor Command", g_TextEditor_editorCommand, false);
		Widget_connectToggleDependency(custom_editor, use_custom);
	}
#endif
	page.appendCheckBox("Console", "Enable Logging", g_Console_enableLogging);
	page.appendCheckBox("", "Load last map on open", g_bLoadLastMap);
}

void Mouse_constructPreferences(PreferencesPage& page) {
	{
		const char* buttons[] = { "2 button", "3 button", };
		page.appendRadio("Mouse Type",  g_glwindow_globals.m_nMouseType, STRING_ARRAY_RANGE(buttons));
	}
	page.appendCheckBox("Right Button", "Activates Context Menu", g_xywindow_globals.m_bRightClick);
}
void Mouse_constructPage(PreferenceGroup& group) {
	PreferencesPage page(group.createPage("Mouse", "Mouse Preferences"));
	Mouse_constructPreferences(page);
}
void Mouse_registerPreferencesPage() {
	PreferencesDialog_addInterfacePage(FreeCaller1<PreferenceGroup&, Mouse_constructPage>());
}


/*!
=========================================================
Games selection dialog
=========================================================
*/

#include <map>

inline const char* xmlAttr_getName(xmlAttrPtr attr) {
	return reinterpret_cast<const char*>(attr->name);
}

inline const char* xmlAttr_getValue(xmlAttrPtr attr) {
	return reinterpret_cast<const char*>(attr->children->content);
}

CGameDescription::CGameDescription(xmlDocPtr pDoc, const CopiedString& gameFile) {
	// read the user-friendly game name
	xmlNodePtr pNode = pDoc->children;

	while (strcmp((const char*)pNode->name, "game") && pNode != 0) {
		pNode = pNode->next;
	}
	if (!pNode) {
		Error("Didn't find 'game' node in the game description file '%s'\n", pDoc->URL);
	}

	for (xmlAttrPtr attr = pNode->properties; attr != 0; attr = attr->next) {
		m_gameDescription.insert(GameDescription::value_type(xmlAttr_getName(attr), xmlAttr_getValue(attr)));
	}

	mGameFile = gameFile;
}

void CGameDescription::Dump() {
	globalOutputStream() << "game description file: " << makeQuoted(mGameFile.c_str()) << "\n";
	for (GameDescription::iterator i = m_gameDescription.begin(); i != m_gameDescription.end(); ++i) {
		globalOutputStream() << (*i).first.c_str() << " = " << makeQuoted((*i).second.c_str()) << "\n";
	}
}

CGameDescription *g_pGameDescription; ///< shortcut to g_GamesDialog.m_pCurrentDescription


#include "stream/textfilestream.h"
#include "container/array.h"
#include "xml/ixml.h"
#include "xml/xmlparser.h"
#include "xml/xmlwriter.h"

#include "preferencedictionary.h"
#include "stringio.h"

const char* const PREFERENCES_VERSION = "1.0";

bool Preferences_Load(PreferenceDictionary& preferences, const char* filename) {
	TextFileInputStream file(filename);
	if (!file.failed()) {
		XMLStreamParser parser(file);
		XMLPreferenceDictionaryImporter importer(preferences, PREFERENCES_VERSION);
		parser.exportXML(importer);
		return true;
	}
	return false;
}

bool Preferences_Save(PreferenceDictionary& preferences, const char* filename) {
	TextFileOutputStream file(filename);
	if (!file.failed()) {
		XMLStreamWriter writer(file);
		XMLPreferenceDictionaryExporter exporter(preferences, PREFERENCES_VERSION);
		exporter.exportXML(writer);
		return true;
	}
	return false;
}

bool Preferences_Save_Safe(PreferenceDictionary& preferences, const char* filename) {
	Array<char> tmpName(filename, filename + strlen(filename) + 1 + 3);
	*(tmpName.end() - 4) = 'T';
	*(tmpName.end() - 3) = 'M';
	*(tmpName.end() - 2) = 'P';
	*(tmpName.end() - 1) = '\0';

	return Preferences_Save(preferences, tmpName.data())
	       && (!file_exists(filename) || file_remove(filename))
	       && file_move(tmpName.data(), filename);
}

void LogConsole_importString(const char* string) {
	g_Console_enableLogging = string_equal(string, "true");
	Sys_LogFile(g_Console_enableLogging);
}
typedef FreeCaller1<const char*, LogConsole_importString> LogConsoleImportStringCaller;

GtkWindow* CGameDialog::BuildDialog() {
	return NULL;
}

static void ScanForGameDefinition() {
	StringOutputStream strGameFilename(256);
	strGameFilename << AppPath_get() << "games/ufoai.game";
	const char *path = strGameFilename.c_str();

	globalOutputStream() << "Scanning for game description files: " << path << '\n';

	xmlDocPtr pDoc = xmlParseFile(path);
	if (pDoc) {
		g_pGameDescription = new CGameDescription(pDoc, path);
		xmlFreeDoc(pDoc);
	} else {
		globalErrorStream() << "XML parser failed on '" << path << "'\n";
		g_pGameDescription = NULL;
	}
}

void CGameDialog::Reset() {
}

void CGameDialog::Init() {
	ScanForGameDefinition();

	ASSERT_NOTNULL(g_pGameDescription);

	g_pGameDescription->Dump();
}

CGameDialog::~CGameDialog() {
	if (GetWidget() != 0) {
		Destroy();
	}
}

CGameDialog g_GamesDialog;


// =============================================================================
// Widget callbacks for PrefsDlg

/**
 * @sa Preferences_Save
 */
static void OnButtonClean (GtkWidget *widget, gpointer data) {
	// make sure this is what the user wants
	if (gtk_MessageBox(GTK_WIDGET(g_Preferences.GetWidget()), "This will close Radiant and clean the corresponding registry entries.\n"
					"Next time you start Radiant it will be good as new. Do you wish to continue?",
					"Reset Registry", eMB_YESNO, eMB_ICONASTERISK) == eIDYES) {
		PrefsDlg *dlg = (PrefsDlg*)data;
		dlg->EndModal (eIDCANCEL);

		g_preferences_globals.disable_ini = true;
		Preferences_Reset();
		gtk_main_quit();
	}
}

// =============================================================================
// PrefsDlg class

void notebook_set_page(GtkWidget* notebook, GtkWidget* page) {
	int pagenum = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), page);
	if (gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook)) != pagenum) {
		gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), pagenum);
	}
}

void PrefsDlg::showPrefPage(GtkWidget* prefpage) {
	notebook_set_page(m_notebook, prefpage);
	return;
}

static void treeSelection(GtkTreeSelection* selection, gpointer data) {
	PrefsDlg *dlg = (PrefsDlg*)data;

	GtkTreeModel* model;
	GtkTreeIter selected;
	if (gtk_tree_selection_get_selected(selection, &model, &selected)) {
		GtkWidget* prefpage;
		gtk_tree_model_get(model, &selected, 1, (gpointer*)&prefpage, -1);
		dlg->showPrefPage(prefpage);
	}
}

typedef std::list<PreferenceGroupCallback> PreferenceGroupCallbacks;

inline void PreferenceGroupCallbacks_constructGroup(const PreferenceGroupCallbacks& callbacks, PreferenceGroup& group) {
	for (PreferenceGroupCallbacks::const_iterator i = callbacks.begin(); i != callbacks.end(); ++i) {
		(*i)(group);
	}
}


inline void PreferenceGroupCallbacks_pushBack(PreferenceGroupCallbacks& callbacks, const PreferenceGroupCallback& callback) {
	callbacks.push_back(callback);
}

typedef std::list<PreferencesPageCallback> PreferencesPageCallbacks;

inline void PreferencesPageCallbacks_constructPage(const PreferencesPageCallbacks& callbacks, PreferencesPage& page) {
	for (PreferencesPageCallbacks::const_iterator i = callbacks.begin(); i != callbacks.end(); ++i) {
		(*i)(page);
	}
}

inline void PreferencesPageCallbacks_pushBack(PreferencesPageCallbacks& callbacks, const PreferencesPageCallback& callback) {
	callbacks.push_back(callback);
}

PreferencesPageCallbacks g_interfacePreferences;
void PreferencesDialog_addInterfacePreferences(const PreferencesPageCallback& callback) {
	PreferencesPageCallbacks_pushBack(g_interfacePreferences, callback);
}
PreferenceGroupCallbacks g_interfaceCallbacks;
void PreferencesDialog_addInterfacePage(const PreferenceGroupCallback& callback) {
	PreferenceGroupCallbacks_pushBack(g_interfaceCallbacks, callback);
}

PreferencesPageCallbacks g_settingsPreferences;
void PreferencesDialog_addSettingsPreferences(const PreferencesPageCallback& callback) {
	PreferencesPageCallbacks_pushBack(g_settingsPreferences, callback);
}
PreferenceGroupCallbacks g_settingsCallbacks;
void PreferencesDialog_addSettingsPage(const PreferenceGroupCallback& callback) {
	PreferenceGroupCallbacks_pushBack(g_settingsCallbacks, callback);
}

void Widget_updateDependency(GtkWidget* self, GtkWidget* toggleButton) {
	gtk_widget_set_sensitive(self, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggleButton)) && GTK_WIDGET_IS_SENSITIVE(toggleButton));
}

void ToggleButton_toggled_Widget_updateDependency(GtkWidget *toggleButton, GtkWidget* self) {
	Widget_updateDependency(self, toggleButton);
}

void ToggleButton_state_changed_Widget_updateDependency(GtkWidget* toggleButton, GtkStateType state, GtkWidget* self) {
	if (state == GTK_STATE_INSENSITIVE) {
		Widget_updateDependency(self, toggleButton);
	}
}

void Widget_connectToggleDependency(GtkWidget* self, GtkWidget* toggleButton) {
	g_signal_connect(G_OBJECT(toggleButton), "state_changed", G_CALLBACK(ToggleButton_state_changed_Widget_updateDependency), self);
	g_signal_connect(G_OBJECT(toggleButton), "toggled", G_CALLBACK(ToggleButton_toggled_Widget_updateDependency), self);
	Widget_updateDependency(self, toggleButton);
}


inline GtkWidget* getVBox(GtkWidget* page) {
	return gtk_bin_get_child(GTK_BIN(page));
}

GtkTreeIter PreferenceTree_appendPage(GtkTreeStore* store, GtkTreeIter* parent, const char* name, GtkWidget* page) {
	GtkTreeIter group;
	gtk_tree_store_append(store, &group, parent);
	gtk_tree_store_set(store, &group, 0, name, 1, page, -1);
	return group;
}

GtkWidget* PreferencePages_addPage(GtkWidget* notebook, const char* name) {
	GtkWidget* preflabel = gtk_label_new(name);
	gtk_widget_show(preflabel);

	GtkWidget* pageframe = gtk_frame_new(name);
	gtk_container_set_border_width(GTK_CONTAINER(pageframe), 4);
	gtk_widget_show(pageframe);

	GtkWidget* vbox = gtk_vbox_new(FALSE, 4);
	gtk_widget_show(vbox);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);
	gtk_container_add(GTK_CONTAINER(pageframe), vbox);

	// Add the page to the notebook
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), pageframe, preflabel);

	return pageframe;
}

class PreferenceTreeGroup : public PreferenceGroup {
	Dialog& m_dialog;
	GtkWidget* m_notebook;
	GtkTreeStore* m_store;
	GtkTreeIter m_group;
public:
	PreferenceTreeGroup(Dialog& dialog, GtkWidget* notebook, GtkTreeStore* store, GtkTreeIter group) :
			m_dialog(dialog),
			m_notebook(notebook),
			m_store(store),
			m_group(group) {
	}
	PreferencesPage createPage(const char* treeName, const char* frameName) {
		GtkWidget* page = PreferencePages_addPage(m_notebook, frameName);
		PreferenceTree_appendPage(m_store, &m_group, treeName, page);
		return PreferencesPage(m_dialog, getVBox(page));
	}
};

GtkWindow* PrefsDlg::BuildDialog() {
	PreferencesDialog_addInterfacePreferences(FreeCaller1<PreferencesPage&, Interface_constructPreferences>());
	Mouse_registerPreferencesPage();
	// Construct the main dialog window. Set a vertical default size as the
	// size_request is too small.
	GtkWindow* dialog = create_floating_window("UFORadiant Preferences", m_parent);
	gtk_window_set_default_size(dialog, -1, 450);

	{
		GtkWidget* mainvbox = gtk_vbox_new(FALSE, 5);
		gtk_container_add(GTK_CONTAINER(dialog), mainvbox);
		gtk_container_set_border_width(GTK_CONTAINER(mainvbox), 5);
		gtk_widget_show(mainvbox);

		{
			GtkWidget* hbox = gtk_hbox_new(FALSE, 5);
			gtk_widget_show(hbox);
			gtk_box_pack_end(GTK_BOX(mainvbox), hbox, FALSE, TRUE, 0);

			{
				GtkButton* button = create_dialog_button("OK", G_CALLBACK(dialog_button_ok), &m_modal);
				gtk_box_pack_end(GTK_BOX(hbox), GTK_WIDGET(button), FALSE, FALSE, 0);
			}
			{
				GtkButton* button = create_dialog_button("Cancel", G_CALLBACK(dialog_button_cancel), &m_modal);
				gtk_box_pack_end(GTK_BOX(hbox), GTK_WIDGET(button), FALSE, FALSE, 0);
			}
			{
				GtkButton* button = create_dialog_button("Clean", G_CALLBACK(OnButtonClean), this);
				gtk_box_pack_end(GTK_BOX(hbox), GTK_WIDGET(button), FALSE, FALSE, 0);
			}
		}

		{
			GtkWidget* hbox = gtk_hbox_new(FALSE, 5);
			gtk_box_pack_start(GTK_BOX(mainvbox), hbox, TRUE, TRUE, 0);
			gtk_widget_show(hbox);

			{
				GtkWidget* sc_win = gtk_scrolled_window_new(0, 0);
				gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc_win), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
				gtk_box_pack_start(GTK_BOX(hbox), sc_win, FALSE, FALSE, 0);
				gtk_widget_show(sc_win);
				gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sc_win), GTK_SHADOW_IN);

				// prefs pages notebook
				m_notebook = gtk_notebook_new();
				// hide the notebook tabs since its not supposed to look like a notebook
				gtk_notebook_set_show_tabs(GTK_NOTEBOOK(m_notebook), FALSE);
				gtk_box_pack_start(GTK_BOX(hbox), m_notebook, TRUE, TRUE, 0);
				gtk_widget_show(m_notebook);


				{
					GtkTreeStore* store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);

					GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
					gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

					{
						GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
						GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes("Preferences", renderer, "text", 0, (char const*)0);
						gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
					}

					{
						GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
						g_signal_connect(G_OBJECT(selection), "changed", G_CALLBACK(treeSelection), this);
					}

					gtk_widget_show(view);

					gtk_container_add(GTK_CONTAINER (sc_win), view);

					{
						/********************************************************************/
						/* Add preference tree options                                      */
						/********************************************************************/
						PreferencePages_addPage(m_notebook, "Front Page");

						{
							GtkWidget* interfacePage = PreferencePages_addPage(m_notebook, "Interface Preferences");
							{
								PreferencesPage preferencesPage(*this, getVBox(interfacePage));
								PreferencesPageCallbacks_constructPage(g_interfacePreferences, preferencesPage);
							}

							GtkTreeIter group = PreferenceTree_appendPage(store, 0, "Interface", interfacePage);
							PreferenceTreeGroup preferenceGroup(*this, m_notebook, store, group);

							PreferenceGroupCallbacks_constructGroup(g_interfaceCallbacks, preferenceGroup);
						}

						{
							GtkWidget* settings = PreferencePages_addPage(m_notebook, "General Settings");
							{
								PreferencesPage preferencesPage(*this, getVBox(settings));
								PreferencesPageCallbacks_constructPage(g_settingsPreferences, preferencesPage);
							}

							GtkTreeIter group = PreferenceTree_appendPage(store, 0, "Settings", settings);
							PreferenceTreeGroup preferenceGroup(*this, m_notebook, store, group);

							PreferenceGroupCallbacks_constructGroup(g_settingsCallbacks, preferenceGroup);
						}
					}

					gtk_tree_view_expand_all(GTK_TREE_VIEW(view));

					g_object_unref(G_OBJECT(store));
				}
			}
		}
	}

	gtk_notebook_set_page(GTK_NOTEBOOK(m_notebook), 0);

	return dialog;
}

preferences_globals_t g_preferences_globals;

PrefsDlg g_Preferences;               // global prefs instance


void PreferencesDialog_constructWindow(GtkWindow* main_window) {
	g_Preferences.m_parent = main_window;
	g_Preferences.Create();
}
void PreferencesDialog_destroyWindow() {
	g_Preferences.Destroy();
}


PreferenceDictionary g_preferences;

PreferenceSystem& GetPreferenceSystem() {
	return g_preferences;
}

class PreferenceSystemAPI {
	PreferenceSystem* m_preferencesystem;
public:
	typedef PreferenceSystem Type;
	STRING_CONSTANT(Name, "*");

	PreferenceSystemAPI() {
		m_preferencesystem = &GetPreferenceSystem();
	}
	PreferenceSystem* getTable() {
		return m_preferencesystem;
	}
};

#include "modulesystem/singletonmodule.h"
#include "modulesystem/moduleregistry.h"

typedef SingletonModule<PreferenceSystemAPI> PreferenceSystemModule;
typedef Static<PreferenceSystemModule> StaticPreferenceSystemModule;
StaticRegisterModule staticRegisterPreferenceSystem(StaticPreferenceSystemModule::instance());

#define PREFS_LOCAL_FILENAME "settings.xml"

void Preferences_Load() {
	StringOutputStream filename(256);
	filename << SettingsPath_get() << PREFS_LOCAL_FILENAME;

	globalOutputStream() << "loading settings from " << filename.c_str() << "\n";

	if (!Preferences_Load(g_preferences, filename.c_str())) {
		globalWarningStream() << "failed to load settings from " << filename.c_str() << "\n";
	}
}

/**
 * @sa OnButtonClean
 */
void Preferences_Save (void)
{
	// we might want to skip the ini settings due to an error
	if (g_preferences_globals.disable_ini)
		return;

	StringOutputStream filename(256);
	filename << SettingsPath_get() << PREFS_LOCAL_FILENAME;
	globalOutputStream() << "saving settings to " << filename.c_str() << "\n";

	if (!Preferences_Save_Safe(g_preferences, filename.c_str())) {
		globalWarningStream() << "failed to save settings to " << filename.c_str() << "\n";
	}
}

void Preferences_Reset (void)
{
	StringOutputStream filename(256);
	filename << SettingsPath_get() << PREFS_LOCAL_FILENAME;
	file_remove(filename.c_str());
}

void PrefsDlg::Init (void) {
}

void PrefsDlg::PostModal (EMessageBoxReturn code) {
	if (code == eIDOK) {
		Preferences_Save();
		UpdateAllWindows();
	}
}

std::vector<const char*> g_restart_required;

void PreferencesDialog_restartRequired(const char* staticName) {
	g_restart_required.push_back(staticName);
}

void PreferencesDialog_showDialog() {
	if (ConfirmModified("Edit Preferences") && g_Preferences.DoModal() == eIDOK) {
		if (!g_restart_required.empty()) {
			StringOutputStream message(256);
			message << "Preference changes require a restart:\n";
			for (std::vector<const char*>::iterator i = g_restart_required.begin(); i != g_restart_required.end(); ++i) {
				message << (*i) << '\n';
			}
			gtk_MessageBox(GTK_WIDGET(MainFrame_getWindow()), message.c_str());
			g_restart_required.clear();
		}
	}
}

void GameName_importString(const char* value) {
	gamename_set(value);
}
typedef FreeCaller1<const char*, GameName_importString> GameNameImportStringCaller;
void GameName_exportString(const StringImportCallback& importer) {
	importer(gamename_get());
}
typedef FreeCaller1<const StringImportCallback&, GameName_exportString> GameNameExportStringCaller;

static void RegisterPreferences(PreferenceSystem& preferences) {
	preferences.registerPreference("log console", LogConsoleImportStringCaller(), BoolExportStringCaller(g_Console_enableLogging));
#ifdef WIN32
	preferences.registerPreference("UseCustomShaderEditor", BoolImportStringCaller(g_TextEditor_useWin32Editor), BoolExportStringCaller(g_TextEditor_useWin32Editor));
#else
	preferences.registerPreference("UseCustomShaderEditor", BoolImportStringCaller(g_TextEditor_useCustomEditor), BoolExportStringCaller(g_TextEditor_useCustomEditor));
	preferences.registerPreference("CustomShaderEditorCommand", CopiedStringImportStringCaller(g_TextEditor_editorCommand), CopiedStringExportStringCaller(g_TextEditor_editorCommand));
#endif

	preferences.registerPreference("GameName", GameNameImportStringCaller(), GameNameExportStringCaller());
}

void Preferences_Init() {
	RegisterPreferences(GetPreferenceSystem());
}
