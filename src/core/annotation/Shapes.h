#pragma once

#include "core/annotation/Annotation.h"

#include <QFont>
#include <QList>
#include <QPointF>

namespace ss {

// 形状类：矩形、椭圆（ANN-20）
class RectAnnotation : public Annotation {
public:
    QString type() const override { return QStringLiteral("rect"); }
    QRectF bounds() const override { return m_rect; }
    void setBounds(const QRectF& r) override { m_rect = r.normalized(); }
    void translate(QPointF d) override { m_rect.translate(d); }
    bool usesFill() const override { return true; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<RectAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

protected:
    QRectF m_rect;
};

class EllipseAnnotation : public RectAnnotation {
public:
    QString type() const override { return QStringLiteral("ellipse"); }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<EllipseAnnotation>(*this); }
};

// 折线 / 箭头（ANN-23、ANN-25 基础）：points 至少两个；arrowEnd 画箭头
class LineAnnotation : public Annotation {
public:
    QString type() const override { return QStringLiteral("line"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<LineAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    QList<QPointF>& points() { return m_points; }
    const QList<QPointF>& points() const { return m_points; }
    void setArrow(bool start, bool end) { m_arrowStart = start; m_arrowEnd = end; }
    bool arrowEnd() const { return m_arrowEnd; }
    bool arrowStart() const { return m_arrowStart; }

    static QPainterPath arrowHead(QPointF from, QPointF to, double width);

private:
    QList<QPointF> m_points;
    bool m_arrowStart = false;
    bool m_arrowEnd = false;
};

// 铅笔（ANN-27）：自由路径；smooth=true 时用二次贝塞尔平滑
class PencilAnnotation : public Annotation {
public:
    QString type() const override { return QStringLiteral("pencil"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<PencilAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    QList<QPointF>& points() { return m_points; }
    const QList<QPointF>& points() const { return m_points; }
    void setSmooth(bool s) { m_smooth = s; }
    QPainterPath path() const;

protected:
    QList<QPointF> m_points;
    bool m_smooth = false;
};

// 荧光笔（ANN-28）：画笔或矩形两种形状；正片叠底或半透明两种混合
class HighlighterAnnotation : public PencilAnnotation {
public:
    enum class Shape { Brush, Rect };
    enum class Blend { Multiply, Translucent };
    QString type() const override { return QStringLiteral("highlighter"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<HighlighterAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setShape(Shape s) { m_shape = s; }
    Shape shape() const { return m_shape; }
    void setRect(const QRectF& r) { m_rect = r.normalized(); }
    void setBlend(Blend b) { m_blend = b; }
    Blend blend() const { return m_blend; }

private:
    Shape m_shape = Shape::Brush;
    Blend m_blend = Blend::Multiply;
    QRectF m_rect;
};

// 马赛克 / 高斯模糊（ANN-29）：画笔、矩形、椭圆三种形状，强度可调；效果作用于下层内容
class EffectAnnotation : public Annotation {
public:
    enum class Effect { Mosaic, Blur };
    enum class Shape { Brush, Rect, Ellipse };
    QString type() const override { return QStringLiteral("effect"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<EffectAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setEffect(Effect e) { m_effect = e; }
    Effect effect() const { return m_effect; }
    void setShape(Shape s) { m_shape = s; }
    Shape shape() const { return m_shape; }
    void setRect(const QRectF& r) { m_rect = r.normalized(); }
    QList<QPointF>& points() { return m_points; }
    void setStrength(int s) { m_strength = std::clamp(s, 1, 100); } // 1~100
    int strength() const { return m_strength; }
    QPainterPath shapePath() const;

private:
    Effect m_effect = Effect::Mosaic;
    Shape m_shape = Shape::Rect;
    QRectF m_rect;
    QList<QPointF> m_points;
    int m_strength = 30;
};

// 文字（ANN-32）：字体、字号、颜色、粗体、斜体；可旋转、可缩放（按包围盒缩放字号）
class TextAnnotation : public Annotation {
public:
    QString type() const override { return QStringLiteral("text"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override { m_pos += d; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<TextAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setText(const QString& t) { m_text = t; }
    const QString& text() const { return m_text; }
    void setFont(const QFont& f) { m_font = f; }
    const QFont& font() const { return m_font; }
    void setPosition(QPointF p) { m_pos = p; } // 左上角
    QPointF position() const { return m_pos; }
    QSizeF textSize() const;

private:
    QString m_text;
    QFont m_font{QStringLiteral("Microsoft YaHei"), 16};
    QPointF m_pos;
};

// 橡皮擦（ANN-36）：画笔、矩形、椭圆形状，只擦标注不擦底图；擦除区域可二次编辑
class EraserAnnotation : public Annotation {
public:
    enum class Shape { Brush, Rect, Ellipse };
    QString type() const override { return QStringLiteral("eraser"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter&, const RenderContext&) const override {} // 擦除由文档渲染时按 eraseShape 处理
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<EraserAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;
    QPainterPath eraseShape() const override;

    void setShape(Shape s) { m_shape = s; }
    Shape shape() const { return m_shape; }
    void setRect(const QRectF& r) { m_rect = r.normalized(); }
    QList<QPointF>& points() { return m_points; }

private:
    Shape m_shape = Shape::Brush;
    QRectF m_rect;
    QList<QPointF> m_points;
};

// 工具函数
QPainterPath brushPath(const QList<QPointF>& points, double width, bool smooth);
QJsonArray pointsToJson(const QList<QPointF>& points);
QList<QPointF> pointsFromJson(const QJsonArray& array);
QRectF boundsOfPoints(const QList<QPointF>& points, double pad);
void scalePoints(QList<QPointF>& points, const QRectF& from, const QRectF& to);

} // namespace ss
