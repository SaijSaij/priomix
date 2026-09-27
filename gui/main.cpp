// SPDX-License-Identifier: GPL-3.0-only
//
// priomix-gui: settings window, tray icon and ChatMix hotkeys for priomix-daemon.
// A pure D-Bus client, it never touches PipeWire or the daemon's config files.

#include <QApplication>
#include <QCommandLineParser>

#include "dbus_client.hpp"
#include "global_shortcuts.hpp"
#include "main_window.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("priomix-gui"));
    QApplication::setApplicationDisplayName(QStringLiteral("priomix"));
    QApplication::setApplicationVersion(QStringLiteral(PRIOMIX_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("io.github.saijsaij.Priomix"));
    // The tray icon keeps the app alive while the window is hidden.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("priomix audio priority settings"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimized(QStringLiteral("minimized"),
                                 QStringLiteral("Start hidden in the system tray."));
    parser.addOption(minimized);
    parser.process(app);

    priomix::gui::DbusClient client;
    priomix::gui::GlobalShortcuts shortcuts(&client);
    priomix::gui::MainWindow window(&client);
    if (!parser.isSet(minimized)) window.show();
    return app.exec();
}
