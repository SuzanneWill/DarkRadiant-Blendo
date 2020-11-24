#pragma once

#include <map>
#include "ieclasscolours.h"

namespace eclass
{

class EClassColourManager :
    public IColourManager
{
private:
    std::map<std::string, Vector3> _overrides;

public:
    // IColourManager implementation

    void addOverrideColour(const std::string& eclass, const Vector3& colour) override;
    void applyColours(const IEntityClassPtr& eclass) override;
    void foreachOverrideColour(const std::function<void(const std::string&, const Vector3&)>& functor) override;
    void removeOverrideColour(const std::string& eclass) override;
    void clearOverrideColours() override;

    // RegisterableModule implementation

    const std::string& getName() const override;
    const StringSet& getDependencies() const override;
    void initialiseModule(const IApplicationContext& ctx) override;
};

}
