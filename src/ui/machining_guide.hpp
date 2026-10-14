#pragma once

#include <QMap>
#include <QObject>
#include <QPainterPath>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <functional>

class QComboBox;
class QSlider;
class QPushButton;
class QPainter;

namespace cnc::ui {

// A dialog-local draft. It never writes to the document or generates machine code.
// All views (including enlarged views) share this model, not copies of a bitmap.
class GuideModel final : public QObject {
    Q_OBJECT
public:
    explicit GuideModel(QObject* parent = nullptr);
    double number(const QString& key) const;
    int integer(const QString& key) const { return qRound(number(key)); }
    QVariant value(const QString& key) const { return values_.value(key); }
    void setValue(const QString& key, const QVariant& value);
    void setContour(const QPainterPath& contour, bool closed = true);
    const QPainterPath& contour() const { return contour_; }
    bool closed() const { return closed_; }
    bool hasSelection() const { return !contour_.isEmpty(); }
signals:
    void changed();
    void valueChanged(const QString& key, const QVariant& value);
private:
    QMap<QString, QVariant> values_;
    QPainterPath contour_;
    bool closed_ = true;
};

class MachiningGuide final : public QWidget {
    Q_OBJECT
public:
    explicit MachiningGuide(const QString& kind, QWidget* parent = nullptr,
                            GuideModel* model = nullptr);
    GuideModel* model() const { return model_; }
    QString kind() const { return kind_; }
    void set_kind(const QString& kind);
    int mode() const;
    void set_mode(int mode);
    QString mode_label_text() const;
    static QString modeKey(const QString& kind);
    static QStringList modeLabels(const QString& kind);
    static QString title(const QString& kind);
    void setPlaying(bool playing);
    bool playing() const { return playing_; }
    bool timerActive() const { return timer_.isActive(); }
    void setProgress(double progress);
    double progress() const { return progress_; }
    void setSpeed(double speed) { speed_ = speed; }
    QPainterPath nominalPath() const { return shape_; }
    QPainterPath trajectory() const { return route_; }
    std::function<void(int)> mode_clicked;
    QSize sizeHint() const override { return {620, 410}; }
    QSize minimumSizeHint() const override { return {240, 185}; }
signals:
    void progressChanged(double progress);
    void playingChanged(bool playing);
    void topicChanged();
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    void rebuild();
    void syncTimer();
    void drawScene(QPainter& painter);
    void drawTool(QPainter& painter, QPointF tip, double width, int type = 0) const;
    void drawRoute(QPainter& painter, const QPainterPath& path, bool tool = true) const;
    GuideModel* model_;
    QString kind_;
    QPainterPath shape_, route_;
    QTransform projection_;
    double tool_width_ = 20;
    QTimer timer_;
    bool playing_ = false;
    double progress_ = .28, speed_ = 1;
};

// Playback is independent of machining parameters: scrub / speed / reverse never
// alter the job. Parameter selectors write to the shared draft only.
QWidget* guidePlayback(MachiningGuide* guide, QWidget* parent = nullptr);
QWidget* guideDemoControls(MachiningGuide* guide, QWidget* parent = nullptr);
void showGuideGallery(QWidget* parent = nullptr);

} // namespace cnc::ui
