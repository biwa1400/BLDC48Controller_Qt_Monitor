#include "mainwindow.h"

#include <QApplication>
#include <QPixmap>
#include <QSplashScreen>
#include <QTimer>

#if defined(Q_OS_WIN) && defined(QT_NO_DEBUG)
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;

#if defined(Q_OS_WIN) && defined(QT_NO_DEBUG)
    QPixmap image;
    HRSRC resource = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), RT_RCDATA);
    if (resource) {
        HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), resource);
        if (loaded) {
            const void *data = LockResource(loaded);
            const DWORD size = SizeofResource(GetModuleHandleW(nullptr), resource);
            if (data && size > 0 && image.loadFromData(static_cast<const uchar *>(data), static_cast<uint>(size), "PNG"))
                image = image.scaledToWidth(900, Qt::SmoothTransformation);
        }
    }
    if (!image.isNull()) {
        QSplashScreen splash(image, Qt::WindowStaysOnTopHint);
        splash.show();
        app.processEvents();
        QTimer::singleShot(3000, &window, [&window, &splash]() {
            window.show();
            splash.finish(&window);
        });
        return app.exec();
    }
#endif

    window.show();
    return app.exec();
}
