#include "QuantizationPreviewWidget.h"

#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>

#include <algorithm>
#include <cmath>

QuantizationPreviewWidget::QuantizationPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
}

void QuantizationPreviewWidget::setPreview(const QuantizationPreviewStats& stats, int maxNotesPerStep)
{
    m_stats = stats;
    m_maxNotesPerStep = std::max(1, maxNotesPerStep);
    update();
}

QSize QuantizationPreviewWidget::sizeHint() const
{
    return {640, 160};
}

QSize QuantizationPreviewWidget::minimumSizeHint() const
{
    return {240, 120};
}

void QuantizationPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(245, 245, 248));
    painter.setPen(QColor(200, 200, 210));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (m_stats.notesPerStep.isEmpty()) {
        painter.setPen(QColor(120, 120, 130));
        painter.drawText(rect(), Qt::AlignCenter, "Nenhuma nota quantizada.");
        return;
    }

    const int margin = 12;
    const int labelH = 18;
    const QRect plot = rect().adjusted(margin, margin, -margin, -(margin + labelH));
    if (plot.width() <= 0 || plot.height() <= 0) {
        return;
    }

    const int stepCount = m_stats.notesPerStep.size();
    // Agrupa steps em colunas para caber na largura (mín. 1px por coluna visual).
    const int columns = std::min(stepCount, std::max(1, plot.width()));
    const double stepsPerColumn = static_cast<double>(stepCount) / columns;
    const int maxCount = std::max(1, m_stats.maxPolyphony);

    for (int col = 0; col < columns; ++col) {
        const int start = static_cast<int>(std::floor(col * stepsPerColumn));
        const int end = static_cast<int>(std::floor((col + 1) * stepsPerColumn));
        int peak = 0;
        bool overCap = false;
        for (int step = start; step < end && step < stepCount; ++step) {
            peak = std::max(peak, m_stats.notesPerStep[step]);
            if (m_stats.notesPerStep[step] > m_maxNotesPerStep) {
                overCap = true;
            }
        }
        if (peak <= 0) {
            continue;
        }

        const int x0 = plot.left() + (col * plot.width()) / columns;
        const int x1 = plot.left() + ((col + 1) * plot.width()) / columns;
        const int barW = std::max(1, x1 - x0);
        const int barH = std::max(1, (peak * plot.height()) / maxCount);
        const QRect bar(x0, plot.bottom() - barH + 1, barW, barH);

        QColor color;
        if (overCap) {
            color = QColor(220, 70, 70);
        } else if (peak >= m_maxNotesPerStep) {
            color = QColor(230, 150, 40);
        } else if (peak >= (m_maxNotesPerStep + 1) / 2) {
            color = QColor(70, 140, 210);
        } else {
            color = QColor(90, 170, 120);
        }
        painter.fillRect(bar, color);
    }

    painter.setPen(QColor(100, 100, 110));
    painter.drawText(
        QRect(margin, height() - margin - labelH, width() - 2 * margin, labelH),
        Qt::AlignLeft | Qt::AlignVCenter,
        QString("0  ·  %1 steps  ·  pico %2 notas/step  ·  vermelho = acima do máximo (%3)")
            .arg(stepCount)
            .arg(m_stats.maxPolyphony)
            .arg(m_maxNotesPerStep));
}
