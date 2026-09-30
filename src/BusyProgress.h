#pragma once

#include <QtCore/QEventLoop>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QWidget>

#include <functional>
#include <utility>

// Executa trabalho pesado fora da thread da UI e mantém um indicador
// indeterminado animado (a UI continua pintando enquanto a tarefa roda).
template<typename Result>
Result runWithBusyProgress(QWidget* parent, const QString& title, const QString& text, std::function<Result()> work)
{
    QProgressDialog dialog(text, QString(), 0, 0, parent);
    dialog.setWindowTitle(title);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setMinimumDuration(0);
    dialog.setCancelButton(nullptr);
    dialog.setAutoClose(false);
    dialog.setAutoReset(false);
    dialog.show();

    Result result{};
    auto* thread = QThread::create([&result, task = std::move(work)]() {
        result = task();
    });

    QEventLoop loop;
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec();
    thread->wait();
    delete thread;
    return result;
}
