// qt_pch.h — Precompiled header for Aegis MediaSuite
// Includes ALL Qt system headers BEFORE any project namespace is opened.
// This ensures GCC 15 two-phase name lookup sees Qt templates in the global namespace.
#pragma once

// Qt Core
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QMap>
#include <QHash>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QUrl>
#include <QDebug>
#include <QMutex>
#include <QTimer>
#include <QThread>
#include <QFuture>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QSharedPointer>
#include <QPointer>
#include <QMetaObject>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QSize>
#include <QSizeF>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QColor>
#include <QImage>

// Qt Multimedia — includes qtvideo.h (Q_ENUM_NS(Rotation))
#include <QVideoSink>
#include <QVideoFrame>

// Qt OpenGL — includes qvectornd.h (operator-, tuple_size)
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLFunctions>

// Qt Qml — includes qqmlprivate.h (qmlRegisterTypeAndRevisions etc.)
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
