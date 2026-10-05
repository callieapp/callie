#pragma once

#include <QLoggingCategory>

// Info and above reach the log file; the terminal shows only warnings unless
// QT_LOGGING_RULES is set. QT_LOGGING_RULES="callie.*=true", as `make run` sets, adds debug.
Q_DECLARE_LOGGING_CATEGORY(lcAuth)
Q_DECLARE_LOGGING_CATEGORY(lcAccounts)
Q_DECLARE_LOGGING_CATEGORY(lcTheme)
Q_DECLARE_LOGGING_CATEGORY(lcSync)
Q_DECLARE_LOGGING_CATEGORY(lcUi)
