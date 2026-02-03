// converter.cpp
#include "converter.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace Aegis {

    QStringList Converter::buildFfmpegArgs(const ConvertJob &job) {
        QStringList args;
        args << "-i" << job.inputPath;
        args << "-y";

        switch(job.preset) {
            case ConvertPreset::AudioMP3:
                args << "-vn" << "-c:a" << "libmp3lame" << "-q:a" << "2";
                break;
            case ConvertPreset::AudioFLAC:
                args << "-vn" << "-c:a" << "flac";
                break;
            case ConvertPreset::VideoMP4:
                args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "23";
                args << "-c:a" << "aac" << "-b:a" << "192k";
                break;
            case ConvertPreset::AudioOnly:
                args << "-vn" << "-c:a" << "copy";
                break;
            case ConvertPreset::Mobile:
                args << "-c:v" << "libx264" << "-preset" << "fast" << "-crf" << "28";
                args << "-vf" << "scale=1280:720";
                args << "-c:a" << "aac" << "-b:a" << "128k";
                break;
        }

        args << "-map_metadata" << "0";
        args << job.outputPath;
        return args;
    }

    ConvertWorker::ConvertWorker(const ConvertJob &job, QObject *parent)
    : QThread(parent), m_job(job) {
    }

    void ConvertWorker::cancel() {
        m_cancel.store(true);
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(5000)) {
                m_process->kill();
            }
        }
    }

    void ConvertWorker::run() {
        m_process = new QProcess();

        QStringList args = buildFfmpegArgs(m_job);
        m_process->setProgram("ffmpeg");
        m_process->setArguments(args);

        connect(m_process, &QProcess::readyReadStandardError, [this]() {
            QByteArray data = m_process->readAllStandardError();
            // Parse progress...
            QRegularExpression re("time=(\\d+):(\\d+):(\\d+\\.\\d+)");
            QRegularExpressionMatch match = re.match(data);
            if (match.hasMatch()) {
                double hours = match.captured(1).toDouble();
                double mins = match.captured(2).toDouble();
                double secs = match.captured(3).toDouble();
                double time = hours * 3600 + mins * 60 + secs;
                // Calculate percentage if duration known
                emit progress(0, "Converting...");
            }
        });

        m_process->start();
        if (!m_process->waitForFinished(-1)) {
            emit finished(false, m_job.outputPath);
            return;
        }

        emit finished(m_process->exitCode() == 0, m_job.outputPath);
        m_process->deleteLater();
        m_process = nullptr;
    }

} // namespace Aegis
