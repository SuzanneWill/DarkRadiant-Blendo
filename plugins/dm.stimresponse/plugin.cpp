#include "imodule.h"

#include "itextstream.h"
#include "ieclass.h"
#include "iscenegraph.h"
#include "icommandsystem.h"
#include "ui/imenumanager.h"
#include "iregistry.h"
#include "iselection.h"
#include "iradiant.h"
#include "iundo.h"
#include "i18n.h"

#include "StimResponseEditor.h"
#include "ResponseEffectTypes.h"

/**
 * Module to register the menu commands for the Stim/Response Editor class.
 */
class StimResponseModule :
	public RegisterableModule
{
public:
	// RegisterableModule implementation
	virtual const std::string& getName() const override
	{
		static std::string _name("StimResponseEditor");
		return _name;
	}

	const StringSet& getDependencies() const override
	{
        static StringSet _dependencies
        {
            MODULE_MENUMANAGER, 
            MODULE_COMMANDSYSTEM
        };

		return _dependencies;
	}

	void initialiseModule(const IApplicationContext& ctx) override
	{
		rMessage() << getName() << "::initialiseModule called." << std::endl;

		// Add the callback event
		GlobalCommandSystem().addCommand("StimResponseEditor", ui::StimResponseEditor::ShowDialog);

		// Add the menu item
        GlobalMenuManager().add("main/entity", 	// menu location path
				"StimResponse", // name
				ui::menu::ItemType::Item,	// type
				_("Stim/Response..."),	// caption
				"stimresponse.png",	// icon
				"StimResponseEditor"); // event name
	}

	void shutdownModule() override
	{
		// Free any resources, the effect types map holds eclass pointers
		ResponseEffectTypes::Clear();
	}
};
typedef std::shared_ptr<StimResponseModule> StimResponseModulePtr;

extern "C" void DARKRADIANT_DLLEXPORT RegisterModule(IModuleRegistry& registry) 
{
	module::performDefaultInitialisation(registry);

	registry.registerModule(StimResponseModulePtr(new StimResponseModule));
}
