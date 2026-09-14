/*
* Audacity: A Digital Audio Editor
*
* Инициализация тестового окружения dubbing: модуль au3wrap (создание
* AudacityProject требует выполненного Au3WrapModule::onInit).
*/
#include "testing/environment.h"

#include "au3wrap/au3wrapmodule.h"

static muse::testing::SuiteEnvironment dubbing_se
    = muse::testing::SuiteEnvironment()
      .setDependencyModules({ new au::au3::Au3WrapModule(), });
