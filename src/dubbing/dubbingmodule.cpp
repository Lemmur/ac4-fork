/*
* Audacity: A Digital Audio Editor
*/
#include "dubbingmodule.h"

#include "internal/dubbingservice.h"

using namespace au::dubbing;

static const std::string mname("dubbing");

std::string DubbingModule::moduleName() const
{
    return mname;
}

muse::modularity::IContextSetup* DubbingModule::newContext(const muse::modularity::ContextPtr& ctx) const
{
    return new DubbingContext(ctx);
}

// =====================================================
// DubbingContext
// =====================================================

void DubbingContext::registerExports()
{
    m_service = std::make_shared<DubbingService>(iocContext());

    ioc()->registerExport<IDubbingProject>(mname, m_service);
}

void DubbingContext::onDeinit()
{
    m_service.reset();
}
