#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <memory>

namespace ss {

// 标注样式（ANN-03、ANN-06、ANN-20）
struct AnnotationStyle {
    QColor stroke = QColor(230, 60, 60);
    int width = 3;
    bool filled = false;
    QColor fill = QColor(230, 60, 60);
    Qt::PenStyle penStyle = Qt::SolidLine;

    bool operator==(const AnnotationStyle&) const = default;
    QJsonObject toJson() const;
    static AnnotationStyle fromJson(const QJsonObject& o);
};

// 渲染上下文：标注画在“下层内容”之上；马赛克 / 模糊 / 荧光笔正片叠底需要读取下层像素
struct RenderContext {
    const QImage* below = nullptr; // 已包含底图与更早标注的画布（与 painter 的设备一致，坐标为图像像素）
    const QImage* base = nullptr;  // 纯底图（不含任何标注）；放大标注“不包含下层标注”时读取
};

// 标注基类：矢量对象，坐标为图像像素（选区内，左上角为原点），每种标注只实现一个 paint（NFR-08）。
class Annotation {
public:
    virtual ~Annotation() = default;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    virtual QString type() const = 0;

    // 几何
    virtual QRectF bounds() const = 0;           // 未旋转的包围盒
    virtual void setBounds(const QRectF& r) = 0; // 等比 / 自由缩放到新包围盒
    virtual void translate(QPointF delta) = 0;
    double rotation() const { return m_rotation; }
    void setRotation(double degrees) { m_rotation = degrees; }
    virtual bool rotatable() const { return true; } // 线条不可旋转（ANN-10）
    // 旋转后的外接矩形（用于失效区域）
    QRectF rotatedBounds() const;
    // 命中测试（图像坐标），tolerance 为像素
    virtual bool hitTest(QPointF p, double tolerance) const;

    // 样式
    const AnnotationStyle& style() const { return m_style; }
    void setStyle(const AnnotationStyle& s) { m_style = s; }
    virtual bool usesFill() const { return false; }

    // 绘制：painter 已处于图像坐标；实现自行应用旋转
    virtual void paint(QPainter& painter, const RenderContext& ctx) const = 0;
    virtual std::unique_ptr<Annotation> clone() const = 0;

    // 序列化（PIN-54 恢复贴图）
    virtual QJsonObject toJson() const;
    virtual void readJson(const QJsonObject& o);

    // 橡皮擦：擦除区域（图像坐标），非橡皮擦返回空路径
    virtual QPainterPath eraseShape() const { return {}; }
    bool isEraser() const { return !eraseShape().isEmpty(); }

protected:
    // 以包围盒中心为轴的旋转变换
    QTransform rotationTransform() const;
    void applyRotation(QPainter& p) const;
    QPen pen() const;

    int m_id = 0;
    double m_rotation = 0;
    AnnotationStyle m_style;
};

// 按 type 字符串创建
std::unique_ptr<Annotation> createAnnotation(const QString& type);
std::unique_ptr<Annotation> annotationFromJson(const QJsonObject& o);

} // namespace ss
