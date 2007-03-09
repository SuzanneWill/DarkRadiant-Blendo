#include "ShaderSelector.h"

#include "gtkutil/glwidget.h"
#include "gtkutil/image.h"
#include "gtkutil/TreeModel.h"
#include "gtkutil/ScrolledFrame.h"
#include "gtkutil/TextColumn.h"
#include "gtkutil/IconTextColumn.h"
#include "gtkutil/VFSTreePopulator.h"
#include "signal/isignal.h"
#include "texturelib.h"
#include "string/string.h"
#include "ishaders.h"
#include "iregistry.h"

#include <gtk/gtk.h>
#include <GL/glew.h>
#include <vector>
#include <string>
#include <map>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace ui
{

/* CONSTANTS */

namespace {
	const char* FOLDER_ICON = "folder16.png";
	const char* TEXTURE_ICON = "icon_texture.png";
	
	// Column enum
	enum {
		NAME_COL, // shader name only (without path)
		FULLNAME_COL, // Full shader name 
		IMAGE_COL, // Icon
		N_COLUMNS
	};
}

// Constructor creates GTK elements
ShaderSelector::ShaderSelector(Client* client, const std::string& prefixes, bool isLightTexture) :
	_infoStore(gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING)),
	_client(client),
	_isLightTexture(isLightTexture)
{
	// Split the given comma-separated list into the vector
	boost::algorithm::split(_prefixes, prefixes, boost::algorithm::is_any_of(","));
	
	// Construct main VBox, and pack in TreeView and info panel
	_widget = gtk_vbox_new(FALSE, 3);
	gtk_box_pack_start(GTK_BOX(_widget), createTreeView(), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(_widget), createPreview(), FALSE, FALSE, 0);
}

// Return the selection to the calling code
std::string ShaderSelector::getSelection() {

	// Get the selection
	GtkTreeIter iter;
	GtkTreeModel* model;
	if (gtk_tree_selection_get_selected(_selection, &model, &iter)) {
		return gtkutil::TreeModel::getString(model, &iter, FULLNAME_COL);
	}
	else {
		// Nothing selected, return empty string
		return "";
	}
}

namespace {

/* Local object that walks the GtkTreeModel and obtains a GtkTreePath locating
 * the given texture. The gtk_tree_model_foreach function requires a pointer to
 * a function, which in this case is a static member of the walker object that
 * accepts a void* pointer to the instance (like other GTK callbacks).
 */
class SelectionFinder {
	
	// String containing the texture to highlight
	std::string _texture;
	
	// The GtkTreePath* pointing to the required texture
	GtkTreePath* _path;
	
public:

	// Constructor
	SelectionFinder(const std::string& selection)
	: _texture(selection),
	  _path(NULL)
	{ }
	
	// Retrieve the found TreePath, which may be NULL if the texture was not
	// found
	GtkTreePath* getPath() {
		return _path;
	}
	
	// Static callback for GTK
	static gboolean forEach(GtkTreeModel* model,
							GtkTreePath* path,
							GtkTreeIter* iter,
							gpointer vpSelf)
	{
		// Get the self instance from the void pointer
		SelectionFinder* self = 
			reinterpret_cast<SelectionFinder*>(vpSelf);
			
		// If the visited row matches the texture to find, set the _path
		// variable and finish, otherwise continue to search
		if (gtkutil::TreeModel::getString(model, iter, FULLNAME_COL) 
			== self->_texture)
		{
			self->_path = gtk_tree_path_copy(path);
			return TRUE; // finish the walk
		}
		else 
		{
			return FALSE;
		}
	} 

};

} // local namespace

// Set the selection in the treeview
void ShaderSelector::setSelection(const std::string& sel) {

	// If the selection string is empty, collapse the treeview and return with
	// no selection
	if (sel.empty()) {
		gtk_tree_view_collapse_all(GTK_TREE_VIEW(_treeView));
		return;
	}

	// Use the local SelectionFinder class to walk the TreeModel
	SelectionFinder finder(sel);
	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(_treeView));
	gtk_tree_model_foreach(model, SelectionFinder::forEach, &finder);
	
	// Get the found TreePath (may be NULL)
	GtkTreePath* path = finder.getPath();
	if (path) {
		// Expand the treeview to display the target row
		gtk_tree_view_expand_to_path(GTK_TREE_VIEW(_treeView), path);
		// Highlight the target row
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(_treeView), path, NULL, false);
		// Make the selected row visible 
		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(_treeView), path, NULL, true, 0.3f, 0.0f);
	}
}

// Local functor to populate the tree view with shader names

namespace {

	// VFSPopulatorVisitor to fill in column data for the populator tree nodes
	class DataInserter
	: public gtkutil::VFSTreePopulator::Visitor
	{
		// Required visit function
		void visit(GtkTreeStore* store, 
				   GtkTreeIter* iter, 
				   const std::string& path,
				   bool isExplicit)
		{

			// Get the display name by stripping off everything before the last
			// slash
			std::string displayName = path.substr(path.rfind("/") + 1);

			// Pathname is the model VFS name for a model, and blank for a folder
			std::string fullPath = isExplicit ? path : "";

			// Pixbuf depends on node type
			GdkPixbuf* pixBuf = isExplicit 
								? gtkutil::getLocalPixbuf(TEXTURE_ICON)
								: gtkutil::getLocalPixbuf(FOLDER_ICON);
			// Fill in the column values
			gtk_tree_store_set(store, iter, 
							   NAME_COL, displayName.c_str(),
							   FULLNAME_COL, fullPath.c_str(),
							   IMAGE_COL, pixBuf,
							   -1);
		} 	
	};

	class ShaderNameFunctor 
	{
	public:
		typedef const char* first_argument_type;

		// Interesting texture prefixes
		ShaderSelector::PrefixList& _prefixes;

		// The populator that gets called to add the parsed elements
		gtkutil::VFSTreePopulator& _populator;

		// Map of prefix to an path pointing to the top-level row that contains
		// instances of this prefix
		std::map<std::string, GtkTreePath*> _iterMap;
		
		// Constructor
		ShaderNameFunctor(gtkutil::VFSTreePopulator& populator, ShaderSelector::PrefixList& prefixes) :
			_prefixes(prefixes), 
			_populator(populator)
		{}
		
		// Destructor. Each GtkTreePath needs to be explicitly freed
		~ShaderNameFunctor() {
			for (std::map<std::string, GtkTreePath*>::iterator i = _iterMap.begin();
					i != _iterMap.end();
				 		++i) 
			{
				gtk_tree_path_free(i->second);
			}
		}
	
		// Functor operator
		void operator() (const char* shaderName) {
			std::string name(shaderName);
			for (ShaderSelector::PrefixList::iterator i = _prefixes.begin();
				 i != _prefixes.end();
				 i++)
			{
				if (!name.empty() && boost::algorithm::istarts_with(name, (*i) + "/")) {
					_populator.addPath(name);
					break; // don't consider any further prefixes
				}
			}
		}
	};
}

// Create the Tree View
GtkWidget* ShaderSelector::createTreeView() {
	// Tree model
	GtkTreeStore* store = gtk_tree_store_new(N_COLUMNS,
											 G_TYPE_STRING, // display name in tree
											 G_TYPE_STRING, // full shader name
											 GDK_TYPE_PIXBUF); 
	
	// Instantiate the helper class that populates the tree according to the paths
	gtkutil::VFSTreePopulator populator(store);
	
	ShaderNameFunctor func(populator, _prefixes);
	GlobalShaderSystem().foreachShaderName(makeCallback1(func));
	
	// Now visit the created GtkTreeIters to load the actual data into the tree
	DataInserter inserter;
	populator.forEachNode(inserter);
	
	// Tree view
	_treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(_treeView), FALSE);
	g_object_unref(store); // tree view owns the reference now

	// Single visible column, containing the directory/shader name and the icon
	gtk_tree_view_append_column(GTK_TREE_VIEW(_treeView),
								gtkutil::IconTextColumn("Value",
														NAME_COL,
														IMAGE_COL));

	// Get selection and connect the changed callback
	_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(_treeView));
	g_signal_connect(G_OBJECT(_selection), 
					 "changed", 
					 G_CALLBACK(_onSelChange), 
					 this);

	// Pack into scrolled window and frame
	return gtkutil::ScrolledFrame(_treeView);
}

// Create the preview panel (GL widget and info table)
GtkWidget* ShaderSelector::createPreview() {

	// HBox contains the preview GL widget along with a texture attributes
	// pane.
	GtkWidget* hbx = gtk_hbox_new(FALSE, 3);

	// GtkGLExt widget
	_glWidget = glwidget_new(false);
	gtk_widget_set_size_request(_glWidget, 128, 128);
	g_signal_connect(G_OBJECT(_glWidget), 
					 "expose-event", 
					 G_CALLBACK(_onExpose), 
					 this);
	GtkWidget* glFrame = gtk_frame_new(NULL);
	gtk_container_add(GTK_CONTAINER(glFrame), _glWidget);
	gtk_box_pack_start(GTK_BOX(hbx), glFrame, FALSE, FALSE, 0);
	
	// Attributes table

	GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(_infoStore));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
	
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
								gtkutil::TextColumn("Attribute", 0));
	
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
								gtkutil::TextColumn("Value", 1));

	gtk_box_pack_start(GTK_BOX(hbx), 
					   gtkutil::ScrolledFrame(tree), 
					   TRUE, TRUE, 0);

	return hbx;
	
} 

// Get the selected shader
IShaderPtr ShaderSelector::getSelectedShader() {
	return GlobalShaderSystem().getShaderForName(getSelection());	
}

// Update the attributes table
void ShaderSelector::updateInfoTable() {
	
	gtk_list_store_clear(_infoStore);

	// Get the selected texture name. If nothing is selected, we just leave the
	// infotable empty.
	std::string selName = getSelection();
	
	// Notify the client of the change to give it a chance to update the infostore
	if (_client != NULL && !selName.empty()) {
		_client->shaderSelectionChanged(selName, _infoStore);
	}
}

// Callback to redraw the GL widget
void ShaderSelector::_onExpose(GtkWidget* widget, 
								GdkEventExpose* ev,
								ShaderSelector* self) 
{
	if (glwidget_make_current(widget) != FALSE) {
		// Get the viewport size from the GL widget
		GtkRequisition req;
		gtk_widget_size_request(widget, &req);
		glViewport(0, 0, req.width, req.height);

		// Initialise
		glClearColor(0.3, 0.3, 0.3, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, req.width, 0, req.height, -100, 100);
		glEnable (GL_TEXTURE_2D);

		// Get the selected texture, and set up OpenGL to render it on
		// the quad.
		IShaderPtr shader = self->getSelectedShader();
		
		bool drawQuad = false;
		
		// Check what part of the shader we should display in the preview 
		if (self->_isLightTexture) {
			// This is a light, take the first layer texture
			const ShaderLayer* first = shader->firstLayer();
			if (first != NULL) {
				TexturePtr tex = shader->firstLayer()->texture();
				glBindTexture (GL_TEXTURE_2D, tex->texture_number);
				drawQuad = true;
			} 
		}
		else {
			// This is an "ordinary" texture, take the editor image
			TexturePtr tex = shader->getTexture();
			if (tex != NULL) {
				glBindTexture (GL_TEXTURE_2D, tex->texture_number);
				drawQuad = true;
			}
		}
		
		if (drawQuad) {
			// Draw a quad to put the texture on
			glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
			glColor3f(1, 1, 1);
			glBegin(GL_QUADS);
			glTexCoord2i(0, 1);
			glVertex2i(0, 0);
			glTexCoord2i(1, 1);
			glVertex2i(req.height, 0);
			glTexCoord2i(1, 0);
			glVertex2i(req.height, req.height);
			glTexCoord2i(0, 0);
			glVertex2i(0, req.height);
			glEnd();
		}

		// Update GtkGlExt buffer
		glwidget_swap_buffers(widget);
	}
}

void ShaderSelector::displayShaderInfo(IShaderPtr shader, GtkListStore* listStore) {
	// Update the infostore in the ShaderSelector
	GtkTreeIter iter;
	
	gtk_list_store_append(listStore, &iter);
	gtk_list_store_set(listStore, &iter, 
					   0, "<b>Shader</b>",
					   1, shader->getName(),
					   -1);
	
	TexturePtr tex = shader->getDiffuse();
	std::string dimStr = intToStr(tex->width) + " x " + intToStr(tex->height);
	
	gtk_list_store_append(listStore, &iter);
	gtk_list_store_set(listStore, &iter,
					   0, "<b>Diffuse</b>",
					   1, dimStr.c_str(),
					   -1);
					   
	// Containing MTR	
	gtk_list_store_append(listStore, &iter);
	gtk_list_store_set(listStore, &iter, 
					   0, "<b>Defined in</b>",
					   1, shader->getShaderFileName(),
					   -1);
}

void ShaderSelector::displayLightShaderInfo(IShaderPtr shader, GtkListStore* listStore) {
	
	const ShaderLayer* first = shader->firstLayer();
	std::string texName = "None";
	if (first != NULL) {
		TexturePtr tex = shader->firstLayer()->texture();
		texName = tex->name;
	}

	GtkTreeIter iter;
	gtk_list_store_append(listStore, &iter);
	gtk_list_store_set(listStore, &iter, 
					   0, "<b>Image map</b>",
					   1, texName.c_str(),
					   -1);

	// Name of file containing the shader
	gtk_list_store_append(listStore, &iter);
	gtk_list_store_set(listStore, &iter, 
					   0, "<b>Defined in</b>",
					   1, shader->getShaderFileName(),
					   -1);

	// Light types, from the IShader

	std::string lightType;
	if (shader->isAmbientLight())
		lightType.append("ambient ");
	if (shader->isBlendLight())
		lightType.append("blend ");
	if (shader->isFogLight())
		lightType.append("fog");
	if (lightType.size() == 0)
		lightType.append("-");
	
	gtk_list_store_append(listStore, &iter);
	gtk_list_store_set(listStore, &iter, 
					   0, "<b>Light flags</b>",
					   1, lightType.c_str(),
					   -1);
}

// Callback for selection changed
void ShaderSelector::_onSelChange(GtkWidget* widget, ShaderSelector* self) {
	self->updateInfoTable();
	gtk_widget_queue_draw(self->_glWidget);
}

} // namespace ui
