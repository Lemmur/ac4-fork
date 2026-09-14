/*
* Audacity: A Digital Audio Editor
*/
#include "dubbingmodule.h"

#include <QtQml>

#include "internal/dubbingservice.h"

#include "panel/lineslistmodel.h"
#include "panel/linesfiltermodel.h"
#include "panel/lineworkspacecontroller.h"

using namespace au::dubbing;

static const std::string mname("dubbing");

static void dubbing_init_qrc()
{
    Q_INIT_RESOURCE(dubbing);
}

std::string DubbingModule::moduleName() const
{
    return mname;
}

void DubbingModule::registerResources()
{
    dubbing_init_qrc();
}

void DubbingModule::registerUiTypes()
{
    //! Регистрация по образцу ProjectSceneModule (projectscenemodule.cpp:214):
    //! URI «Audacity.Dubbing» соответствует qml/Audacity/Dubbing/qmldir в qrc
    //! (движок добавляет «:/qml» в пути импорта — uiengine.cpp:77).
    qmlRegisterType<LinesListModel>("Audacity.Dubbing", 1, 0, "LinesListModel");
    qmlRegisterType<LinesFilterModel>("Audacity.Dubbing", 1, 0, "LinesFilterModel");
    qmlRegisterType<LineworkspaceController>("Audacity.Dubbing", 1, 0, "LineworkspaceController");
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
