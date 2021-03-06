#include "ParticlePropertyEditor.h"
#include "PropertyEditorFactory.h"
#include "../../ui/particles/ParticleSelector.h"

#include "ientity.h"

#include "radiant_i18n.h"

#include <gtk/gtk.h>

namespace ui {

// Main constructor
ParticlePropertyEditor::ParticlePropertyEditor (Entity* entity, const std::string& name, const std::string& options) :
	_entity(entity), _key(name)
{
	_widget = gtk_vbox_new(FALSE, 0);

	// Horizontal box contains the browse button
	GtkWidget* hbx = gtk_hbox_new(FALSE, 3);
	gtk_container_set_border_width(GTK_CONTAINER(hbx), 3);

	// Browse button
	GtkWidget* browseButton = gtk_button_new_with_label(_("Choose particle..."));
	gtk_button_set_image(GTK_BUTTON(browseButton), gtk_image_new_from_pixbuf(PropertyEditorFactory::getPixbufFor(
			"particle")));

	g_signal_connect(G_OBJECT(browseButton),
			"clicked",
			G_CALLBACK(_onBrowseButton),
			this);
	gtk_box_pack_start(GTK_BOX(hbx), browseButton, TRUE, FALSE, 0);

	// Pack hbox into vbox (to limit vertical size), then edit frame
	GtkWidget* vbx = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbx), hbx, TRUE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(_widget), vbx, TRUE, TRUE, 0);
}

/* GTK CALLBACKS */

void ParticlePropertyEditor::_onBrowseButton (GtkWidget* w, ParticlePropertyEditor* self)
{
	// Use a SoundChooser dialog to get a selection from the user
	ParticleSelector chooser;
	std::string selection = chooser.chooseParticle();
	if (!selection.empty()) {
		// greebo: Instantiate a scoped object to make this operation undoable
		UndoableCommand command("entitySetProperty");

		// Apply the change to the entity
		self->_entity->setKeyValue(self->_key, selection);
	}
}

} // namespace ui
