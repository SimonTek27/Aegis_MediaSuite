// disc_labelmaker.h
// Integrated CD/DVD/Blu-ray Label Designer for Aegis
// Bridges Library (metadata), Disc (ripper), and DiscBurner (writer)

#pragma once

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QColor>
#include <QVariantList>
#include <QUrl>
#include <QPrinter>
#include <QFutureWatcher>
#include <memory>
#include <vector>
#include <functional>

// Forward declarations for Aegis integration
namespace Aegis {
    class Library;          // From library.h
    class Disc;             // From disc.h
    class CDBurner;         // From discburner.h
    struct BurnJob;         // From discburner.h
}

#include "library.h"
#include "discburner.h"   // Needed for complete BurnJob definition

namespace Aegis {
    // Use LibraryTrack directly to avoid name clash with audio_daw.h::Track
    using LabelTrack = LibraryTrack;

    enum class LabelSheetType {
        A4, Letter, Custom,
        Avery8692, Avery8693, Avery8915, Avery8931,
        MemorexCD, StaplesCD, VerbatimCD
    };

    enum class TemplateType {
        Disc120mm, Disc80mm, DiscBusinessCard,
        JewelCaseFront, JewelCaseBack, JewelCaseInsert,
        SlimCaseFront, SlimCaseBack,
        DVDCaseCover, DVDCaseInsert,
        BluRayCaseCover, BluRayCaseInsert,
        CustomSize
    };

    enum class ElementType {
        Text, Image, Shape, TrackList, Spacer, Barcode
    };

    // Serializable design element base
    class LabelElement {
    public:
        virtual ~LabelElement() = default;

        QString id;
        ElementType type;
        QRectF rect;              // Position in mm
        double rotation{0.0};
        double opacity{1.0};
        bool locked{false};
        bool visible{true};

        virtual void render(QPainter* painter, const QRectF& targetRect) const = 0;
        virtual QVariantMap serialize() const;
        virtual void deserialize(const QVariantMap& map);
    };

    class TextElement : public LabelElement {
    public:
        TextElement();

        QString text;
        QFont font;
        QColor color{Qt::black};
        Qt::Alignment alignment{Qt::AlignCenter};
        bool autoFit{true};
        bool circularText{false};      // For disc labels (curved text)
        QPointF circleCenter;          // Relative to rect
        double circleRadius{0};        // 0 = auto

        void render(QPainter* painter, const QRectF& targetRect) const override;
        QVariantMap serialize() const override;
        void deserialize(const QVariantMap& map) override;
    };

    class ImageElement : public LabelElement {
    public:
        ImageElement();

        QUrl source;
        bool maintainAspect{true};
        Qt::AspectRatioMode aspectMode{Qt::KeepAspectRatio};
        QColor tint;                   // Multiply blend
        int blurRadius{0};

        void render(QPainter* painter, const QRectF& targetRect) const override;
        QVariantMap serialize() const override;
        void deserialize(const QVariantMap& map) override;

    private:
        mutable QImage cache;
        mutable QString cacheKey;
        QImage loadImage() const;
    };

    class ShapeElement : public LabelElement {
    public:
        ShapeElement();

        enum ShapeType { Rectangle, Ellipse, Circle, Line, Star, Polygon };
        ShapeType shapeType{Rectangle};
        QColor fillColor{Qt::transparent};
        QColor strokeColor{Qt::black};
        double strokeWidth{0.5};
        Qt::PenStyle strokeStyle{Qt::SolidLine};
        int starPoints{5};             // For star shape
        QPolygonF customPolygon;

        void render(QPainter* painter, const QRectF& targetRect) const override;
        QVariantMap serialize() const override;
        void deserialize(const QVariantMap& map) override;
    };

    class TrackListElement : public LabelElement {
    public:
        TrackListElement();

        QVector<QString> tracks;       // Track titles
        QVector<int> durations;        // Seconds
        QFont font{"Arial", 9};
        QColor color{Qt::black};
        bool showTrackNumbers{true};
        bool showDurations{true};
        bool totalDuration{true};      // Show "Total: 45:30" at bottom
        QString formatString{"%n. %t %d"}; // %n=number, %t=title, %d=duration

        void render(QPainter* painter, const QRectF& targetRect) const override;
        QVariantMap serialize() const override;
        void deserialize(const QVariantMap& map) override;

        // Convenience builder
        void setFromLibraryQuery(const QVector<LibraryTrack>& libraryTracks);
    };

    struct TemplateGeometry {
        QString name;
        TemplateType type;
        QSizeF size;                   // mm
        QMarginsF margins;             // Printable margins mm

        // Disc-specific
        double innerDiameter{0};
        double outerDiameter{0};
        double hubBleed{2.0};          // Inner unprintable area

        // Case-specific
        double spineWidth{0};
        bool isWraparound{false};

        // Helpers
        bool isDisc() const { return outerDiameter > 0; }
        bool isCase() const { return spineWidth > 0; }
        QRectF printableArea() const {
            return QRectF(margins.left(), margins.top(),
                          size.width() - margins.left() - margins.right(),
                          size.height() - margins.top() - margins.bottom());
        }
    };

    // Main controller - integrates with Library, Disc, DiscBurner
    class DiscLabelMaker : public QObject {
        Q_OBJECT
        Q_PROPERTY(TemplateType currentTemplate READ currentTemplate WRITE setCurrentTemplate NOTIFY currentTemplateChanged)
        Q_PROPERTY(LabelSheetType paperType READ paperType WRITE setPaperType NOTIFY paperChanged)
        Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
        Q_PROPERTY(bool hasUnsavedChanges READ modified NOTIFY modifiedChanged)
        Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY projectChanged)
        Q_PROPERTY(QSizeF templateSize READ templateSize NOTIFY currentTemplateChanged)

    public:
        explicit DiscLabelMaker(QObject* parent = nullptr);
        ~DiscLabelMaker() override;

        // === TEMPLATE MANAGEMENT ===
        void setCurrentTemplate(TemplateType type);
        TemplateType currentTemplate() const { return m_template; }
        Q_INVOKABLE QVariantList availableTemplates() const;
        Q_INVOKABLE QString templateDisplayName(TemplateType type) const;
        TemplateGeometry geometry() const;
        QSizeF templateSize() const { return geometry().size; }
        // QML-accessible version of geometry() returning a plain map
        Q_INVOKABLE QVariantMap geometryMap() const {
            const TemplateGeometry g = geometry();
            QVariantMap m;
            m["name"]          = g.name;
            m["width"]         = g.size.width();
            m["height"]        = g.size.height();
            m["innerDiameter"] = g.innerDiameter;
            m["outerDiameter"] = g.outerDiameter;
            m["spineWidth"]    = g.spineWidth;
            m["isDisc"]        = g.isDisc();
            return m;
        }

        // === PAPER/PAGE SETUP ===
        void setPaperType(LabelSheetType type);
        LabelSheetType paperType() const { return m_paper; }
        Q_INVOKABLE QVariantList availablePaperTypes() const;

        // === ELEMENT EDITING ===
        Q_INVOKABLE QString addText(const QString& text, const QRectF& rectMm);
        Q_INVOKABLE QString addImage(const QUrl& source, const QRectF& rectMm);
        Q_INVOKABLE QString addShape(int shapeType, const QRectF& rectMm);
        Q_INVOKABLE QString addTrackList(const QRectF& rectMm);
        Q_INVOKABLE void removeElement(const QString& id);
        Q_INVOKABLE void clearElements();
        Q_INVOKABLE void updateElement(const QString& id, const QVariantMap& props);
        Q_INVOKABLE void moveElement(const QString& id, const QPointF& newPosMm);
        Q_INVOKABLE void resizeElement(const QString& id, const QSizeF& newSizeMm);
        Q_INVOKABLE void rotateElement(const QString& id, double degrees);
        Q_INVOKABLE void bringToFront(const QString& id);
        Q_INVOKABLE void sendToBack(const QString& id);
        Q_INVOKABLE QVariantList elements() const;
        Q_INVOKABLE QVariantMap element(const QString& id) const;

        // === INTEGRATION WITH AEGIS CORE ===

        // Library integration - async fill from database
        Q_INVOKABLE void autoFillFromLibraryTrack(int trackId, Library* library);
        Q_INVOKABLE void autoFillFromLibraryAlbum(const QString& album, Library* library);
        Q_INVOKABLE void autoFillFromLibraryPlaylist(const QString& playlistName, Library* library);

        // Disc integration - fill from optical drive
        Q_INVOKABLE void autoFillFromDisc(Disc* disc);

        // DiscBurner integration - print for burned disc
        void attachToBurner(CDBurner* burner);  // Auto-connect signals
        Q_INVOKABLE void prepareForBurnJob(const BurnJob& job);

        // === DESIGN UTILITIES ===
        Q_INVOKABLE void setBackgroundColor(const QColor& color);
        Q_INVOKABLE void setBackgroundImage(const QUrl& source);
        Q_INVOKABLE void centerElement(const QString& id, bool horizontal = true, bool vertical = true);
        Q_INVOKABLE void alignToGrid(const QString& id, double gridSizeMm = 5.0);
        Q_INVOKABLE void snapToGuides(const QString& id);

        // === RENDERING & EXPORT ===
        Q_INVOKABLE QImage render(int dpi = 300) const;
        Q_INVOKABLE QImage renderPreview(const QSize& pixelSize) const;
        Q_INVOKABLE bool saveAsImage(const QString& path, const QString& format = "png", int dpi = 300);
        Q_INVOKABLE bool exportToPDF(const QString& path);

        // === PRINTING ===
        Q_INVOKABLE void print();
        Q_INVOKABLE void printPreview();
        Q_INVOKABLE void calibratePrinter();  // Print alignment sheet

        // Printer calibration offset (for misaligned printers)
        Q_INVOKABLE void setPrintOffsetMm(const QPointF& offset);
        QPointF printOffsetMm() const { return m_printOffset; }

        // === PROJECT I/O ===
        Q_INVOKABLE bool saveProject(const QString& path);
        Q_INVOKABLE bool loadProject(const QString& path);
        Q_INVOKABLE void newProject();
        bool modified() const { return m_modified; }
        QString currentProjectPath() const { return m_projectPath; }

        // === STATIC UTILS ===
        static TemplateGeometry geometryForType(TemplateType type);
        static double mmToInches(double mm) { return mm / 25.4; }
        static double inchesToMm(double inches) { return inches * 25.4; }
        static double mmToPixels(double mm, int dpi) { return (mm / 25.4) * dpi; }

    signals:
        void currentTemplateChanged();
        void paperChanged();
        void elementsChanged();
        void elementUpdated(const QString& id);
        void modifiedChanged();
        void projectChanged();
        void renderRequested();
        void printStarted();
        void printFinished(bool success);
        void libraryQueryFinished(bool success);

        // Integration signals
        void labelPreparedForBurn(const BurnJob& job);
        void autoFillCompleted(const QString& source);

    private slots:
        void onLibraryQueryFinished();
        void onDiscScanned();
        void onBurnFinished(bool success, const QString& message);

    private:
        void markModified();
        void addElement(std::unique_ptr<LabelElement> elem);
        LabelElement* findElement(const QString& id);
        void renderToPainter(QPainter* painter, const QRectF& rect, bool forPrint = false) const;

        // Element storage
        std::vector<std::unique_ptr<LabelElement>> m_elements;
        int m_nextId{0};

        // State
        TemplateType m_template{TemplateType::Disc120mm};
        LabelSheetType m_paper{LabelSheetType::A4};
        QColor m_bgColor{Qt::white};
        QUrl m_bgImage;
        QPointF m_printOffset{0, 0};
        bool m_modified{false};
        QString m_projectPath;

        // Async integration
        Library* m_pendingLibrary{nullptr};
        std::unique_ptr<QFutureWatcher<std::vector<LibraryTrack>>> m_libraryWatcher;
        BurnJob m_pendingBurnJob;  ///< Stored by attachToBurner/setPendingBurnJob before burning starts

        // Printer
        std::unique_ptr<QPrinter> m_printer;

        // Connections (for cleanup)
        QMetaObject::Connection m_discConnection;
        QMetaObject::Connection m_burnerConnection;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::LabelSheetType)
Q_DECLARE_METATYPE(Aegis::TemplateType)
Q_DECLARE_METATYPE(Aegis::ElementType)
