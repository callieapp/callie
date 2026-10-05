#pragma once

#include <QLoggingCategory>

// Only warnings show by default, so the CLI stays quiet. Enable everything with
// QT_LOGGING_RULES="callie.*=true", which `make run` does.
Q_DECLARE_LOGGING_CATEGORY(lcAuth)
Q_DECLARE_LOGGING_CATEGORY(lcAccounts)
Q_DECLARE_LOGGING_CATEGORY(lcTheme)
Q_DECLARE_LOGGING_CATEGORY(lcSync)
