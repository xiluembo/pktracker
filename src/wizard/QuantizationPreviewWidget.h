#pragma once

#include "generator/NoteMapping.h"

#include <QtWidgets/QWidget>

// Heatmap horizontal da polifonia por step após a quantização.
class QuantizationPreviewWidget : public QWidget {
public:
    explicit QuantizationPreviewWidget(QWidget* parent = nullptr);

    void setPreview(const QuantizationPreviewStats& stats, int maxNotesPerStep);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QuantizationPreviewStats m_stats;
    int m_maxNotesPerStep = 6;
};
