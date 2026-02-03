// disc_labelmaker.cpp
// CD/DVD/Blu-ray Label and Cover Designer for Aegis

#include "disc_labelmaker.h"
#include "library.h"
#include "disc.h"
#include "discburner.h"
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QtMath>
#include <QFontMetrics>
#include <algorithm>

namespace Aegis {

    TemplateGeometry DiscLabelMaker::geometryForType(TemplateType type) {
        TemplateGeometry g;
        g.type = type;

        switch (type) {
            case TemplateType::Disc120mm:
                g.name = QObject::tr("Standard CD/DVD/BD (120mm)");
                g.size = QSizeF(120, 120);
                g.outerDiameter = 120.0;
                g.innerDiameter = 15.0;  // Small hub
                g.hubBleed = 2.0;
                break;

            case TemplateType::Disc80mm:
                g.name = QObject::tr("Mini CD (80mm)");
                g.size = QSizeF(80, 80);
                g.outerDiameter = 80.0;
                g.innerDiameter = 15.0;
                break;

            case TemplateType::JewelCaseFront:
                g.name = QObject::tr("Jewel Case Front Cover");
                g.size = QSizeF(120, 120);
                g.margins = QMarginsF(3, 3, 3, 3);  // Bleed area
                break;

            case TemplateType::JewelCaseBack:
                g.name = QObject::tr("Jewel Case Back (w/ Spine)");
                g.size = QSizeF(151, 118);  // Front (72.5) + Spine (6) + Back (72.5)
                g.spineWidth = 6.0;
                break;

            case TemplateType::JewelCaseInsert:
                g.name = QObject::tr("Jewel Case 2-Panel Insert");
                g.size = QSizeF(242, 120);  // Folded: 2 * 120 + 2mm overlap
                break;

            case TemplateType::DVDCaseCover:
                g.name = QObject::tr("DVD Case Wrap-around");
                g.size = QSizeF(272, 184);  // Front (129) + Spine (14) + Back (129)
                g.spineWidth = 14.0;
                g.isWraparound = true;
                break;

            case TemplateType::BluRayCaseCover:
                g.name = QObject::tr("Blu-ray Case Wrap-around");
                g.size = QSizeF(312, 174);  // BD cases are taller but thinner spine
                g.spineWidth = 12.0;
                g.isWraparound = true;
                break;

            default:
                g.name = QObject::tr("Custom");
                g.size = QSizeF(120, 120);
        }
        return g;
    }

    TextElement::TextElement() {
        type = ElementType::Text;
        font = QFont("Arial", 11);
    }

    ImageElement::ImageElement() {
        type = ElementType::Image;
    }

    ShapeElement::ShapeElement() {
        type = ElementType::Shape;
    }

    TrackListElement::TrackListElement() {
        type = ElementType::TrackList;
    }

    void TextElement::render(QPainter* painter, const QRectF& targetRect) const {
        if (text.isEmpty()) return;

        painter->setPen(color);

        if (circularText && circleRadius > 0) {
            // Circular text path (for disc labels)
            QPointF center = targetRect.center();
            double radius = circleRadius;
            if (radius <= 0) radius = std::min(targetRect.width(), targetRect.height()) / 2.0 - 5;

            QFontMetrics fm(font);
            double anglePerChar = 0.15;  // radians, approx
            double totalAngle = text.length() * anglePerChar;
            double startAngle = -M_PI / 2.0 - totalAngle / 2.0;  // Center at top

            painter->setFont(font);

            for (int i = 0; i < text.length(); ++i) {
                double angle = startAngle + (i * anglePerChar);
                QPointF pos(center.x() + radius * cos(angle),
                            center.y() + radius * sin(angle));

                painter->save();
                painter->translate(pos);
                painter->rotate(qRadiansToDegrees(angle) + 90);
                painter->drawText(0, 0, QString(text[i]));
                painter->restore();
            }
        } else {
            // Standard text with auto-fit
            QFont drawFont = font;
            if (autoFit) {
                QFontMetricsF fm(drawFont);
                QRectF bounds = fm.boundingRect(text);

                // Scale down if needed
                if (bounds.width() > targetRect.width() || bounds.height() > targetRect.height()) {
                    double scale = std::min(targetRect.width() / bounds.width(),
                                            targetRect.height() / bounds.height());
                    drawFont.setPointSizeF(font.pointSizeF() * scale * 0.9);
                }
            }
            painter->setFont(drawFont);
            painter->drawText(targetRect, alignment | Qt::TextWordWrap, text);
        }
    }

    void ImageElement::render(QPainter* painter, const QRectF& targetRect) const {
        QImage img = loadImage();
        if (img.isNull()) return;

        QRectF drawRect = targetRect;
        if (maintainAspect) {
            QSizeF sz = img.size();
            sz.scale(targetRect.size(), aspectMode);
            drawRect.setSize(sz);
            drawRect.moveCenter(targetRect.center());
        }

        if (blurRadius > 0) {
            // Simple box blur could be applied here via QImage::blurred (Qt 6.8+)
            // or using QGraphicsBlurEffect offline
        }

        painter->drawImage(drawRect, img);

        if (tint.isValid()) {
            painter->fillRect(drawRect, QColor(tint.red(), tint.green(), tint.blue(), 80));
        }
    }

    QImage ImageElement::loadImage() const {
        QString key = source.toString();
        if (cacheKey == key && !cache.isNull()) return cache;

        if (source.isLocalFile()) {
            cache.load(source.toLocalFile());
        } else {
            // Handle qrc:/ or http
            // For http, would need async loading
            cache.load(source.path());
        }
        cacheKey = key;
        return cache;
    }

    void ShapeElement::render(QPainter* painter, const QRectF& targetRect) const {
        QPen pen(strokeColor, strokeWidth, strokeStyle);
        painter->setPen(pen);
        painter->setBrush(fillColor);

        switch (shapeType) {
            case Rectangle:
                painter->drawRect(targetRect);
                break;
            case Ellipse:
                painter->drawEllipse(targetRect);
                break;
            case Circle: {
                double d = std::min(targetRect.width(), targetRect.height());
                QRectF r(0, 0, d, d);
                r.moveCenter(targetRect.center());
                painter->drawEllipse(r);
                break;
            }
            case Line:
                painter->drawLine(targetRect.topLeft(), targetRect.bottomRight());
                break;
            case Star: {
                QPolygonF star;
                double cx = targetRect.center().x();
                double cy = targetRect.center().y();
                double outer = std::min(targetRect.width(), targetRect.height()) / 2.0;
                double inner = outer * 0.4;

                for (int i = 0; i < starPoints * 2; ++i) {
                    double angle = M_PI * i / starPoints - M_PI/2;
                    double r = (i % 2 == 0) ? outer : inner;
                    star << QPointF(cx + r * cos(angle), cy + r * sin(angle));
                }
                painter->drawPolygon(star);
                break;
            }
            case Polygon:
                if (!customPolygon.isEmpty()) {
                    // Scale polygon to fit targetRect
                    QRectF bounds = customPolygon.boundingRect();
                    QTransform t;
                    t.translate(targetRect.x(), targetRect.y());
                    t.scale(targetRect.width() / bounds.width(),
                            targetRect.height() / bounds.height());
                    painter->drawPolygon(t.map(customPolygon));
                }
                break;
        }
    }

    void TrackListElement::render(QPainter* painter, const QRectF& targetRect) const {
        painter->setFont(font);
        painter->setPen(color);
        QFontMetricsF fm(font);
        double lineHeight = fm.height();
        double y = targetRect.top();

        // Calculate available lines
        int maxLines = static_cast<int>(targetRect.height() / lineHeight);
        int linesToDraw = std::min(tracks.size(), maxLines);

        for (int i = 0; i < linesToDraw; ++i) {
            QString line = formatString;
            if (showTrackNumbers) {
                line.replace("%n", QString::number(i + 1));
            } else {
                line.replace("%n.", "");
            }

            line.replace("%t", tracks[i]);

            if (showDurations && i < durations.size()) {
                int s = durations[i];
                QString d = QString("%1:%2").arg(s/60, 2, 10, QChar('0')).arg(s%60, 2, 10, QChar('0'));
                line.replace("%d", d);
            } else {
                // Remove duration placeholder and surrounding space/punctuation
                line.replace(" %d", "");
                line.replace("%d", "");
            }

            painter->drawText(QPointF(targetRect.left(), y + fm.ascent()), line);
            y += lineHeight;
        }
    }

    void TrackListElement::setFromLibraryQuery(const QVector<Track>& libraryTracks) {
        tracks.clear();
        durations.clear();
        for (const auto& t : libraryTracks) {
            tracks.append(QString::fromStdString(t.title));
            durations.append(t.duration);
        }
    }

    // === MAIN CLASS IMPLEMENTATION ===

    DiscLabelMaker::DiscLabelMaker(QObject* parent)
    : QObject(parent)
    , m_libraryWatcher(std::make_unique<QFutureWatcher<QVector<Track>>>())
    , m_printer(std::make_unique<QPrinter>(QPrinter::HighResolution))
    {
        m_printer->setPageSize(QPageSize::A4);
        m_printer->setPageOrientation(QPageLayout::Portrait);
        m_printer->setFullPage(true);

        connect(m_libraryWatcher.get(), &QFutureWatcher<QVector<Track>>::finished,
                this, &DiscLabelMaker::onLibraryQueryFinished);
    }

    DiscLabelMaker::~DiscLabelMaker() = default;

    void DiscLabelMaker::setCurrentTemplate(TemplateType type) {
        if (m_template == type) return;

        m_template = type;

        // Auto-add guide lines for discs
        if ((type == TemplateType::Disc120mm || type == TemplateType::Disc80mm) && m_elements.empty()) {
            auto geom = geometryForType(type);

            // Outer cut line
            auto outer = std::make_unique<ShapeElement>();
            outer->id = "guide_outer";
            outer->shapeType = ShapeElement::Circle;
            outer->rect = QRectF(0, 0, geom.outerDiameter, geom.outerDiameter);
            outer->strokeColor = QColor(180, 180, 180);
            outer->fillColor = Qt::transparent;
            outer->strokeStyle = Qt::DashLine;
            outer->locked = true;
            addElement(std::move(outer));

            // Inner hub line
            if (geom.innerDiameter > 0) {
                auto inner = std::make_unique<ShapeElement>();
                inner->id = "guide_inner";
                inner->shapeType = ShapeElement::Circle;
                double xy = (geom.outerDiameter - geom.innerDiameter) / 2.0;
                inner->rect = QRectF(xy, xy, geom.innerDiameter, geom.innerDiameter);
                inner->strokeColor = QColor(200, 100, 100);  // Reddish for "don't print"
                inner->fillColor = QColor(240, 240, 240, 100);
                inner->locked = true;
                addElement(std::move(inner));
            }
        }

        emit currentTemplateChanged();
        markModified();
    }

    QVariantList DiscLabelMaker::availableTemplates() const {
        QVariantList list;
        for (auto t : {TemplateType::Disc120mm, TemplateType::Disc80mm,
            TemplateType::JewelCaseFront, TemplateType::JewelCaseBack,
            TemplateType::JewelCaseInsert, TemplateType::DVDCaseCover,
            TemplateType::BluRayCaseCover}) {
            QVariantMap m;
            m["type"] = static_cast<int>(t);
            auto g = geometryForType(t);
            m["name"] = g.name;
            m["width"] = g.size.width();
            m["height"] = g.size.height();
            m["isDisc"] = g.isDisc();
            m["isCase"] = g.isCase();
            list.append(m);
            }
            return list;
    }

    QString DiscLabelMaker::templateDisplayName(TemplateType type) const {
        return geometryForType(type).name;
    }

    TemplateGeometry DiscLabelMaker::geometry() const {
        return geometryForType(m_template);
    }

    // === LIBRARY INTEGRATION ===

    void DiscLabelMaker::autoFillFromLibraryTrack(int trackId, Library* library) {
        if (!library) return;

        m_pendingLibrary = library;

        // Query single track
        auto future = library->search(QString("id:%1").arg(trackId), 1);
        m_libraryWatcher->setFuture(future);
    }

    void DiscLabelMaker::autoFillFromLibraryAlbum(const QString& album, Library* library) {
        if (!library) return;

        m_pendingLibrary = library;
        auto future = library->searchAlbumTracks(album);  // Assumes Library has this method
        m_libraryWatcher->setFuture(future);
    }

    void DiscLabelMaker::onLibraryQueryFinished() {
        auto tracks = m_libraryWatcher->result();

        if (tracks.isEmpty()) {
            emit libraryQueryFinished(false);
            return;
        }

        clearElements();
        auto geom = geometry();

        if (!tracks.isEmpty()) {
            const Track& first = tracks[0];

            // Title at top (centered for discs, top for cases)
            if (m_template == TemplateType::Disc120mm || m_template == TemplateType::Disc80mm) {
                // Circular text around top
                auto titleElem = std::make_unique<TextElement>();
                titleElem->text = QString::fromStdString(first.title);
                titleElem->rect = QRectF(10, 10, geom.size.width() - 20, 30);
                titleElem->circularText = true;
                titleElem->circleRadius = geom.outerDiameter / 2.0 - 10;
                titleElem->font.setPointSize(14);
                titleElem->font.setBold(true);
                addElement(std::move(titleElem));

                // Artist at bottom (inverted curve)
                auto artistElem = std::make_unique<TextElement>();
                artistElem->text = QString::fromStdString(first.artist);
                artistElem->rect = QRectF(10, geom.size.height() - 40, geom.size.width() - 20, 30);
                artistElem->circularText = true;
                artistElem->circleRadius = geom.outerDiameter / 2.0 - 10;
                artistElem->font.setPointSize(11);
                addElement(std::move(artistElem));

            } else if (m_template == TemplateType::JewelCaseFront) {
                // Standard jewel case layout
                auto titleElem = std::make_unique<TextElement>();
                titleElem->text = QString::fromStdString(first.album);
                titleElem->rect = QRectF(5, 90, 110, 20);
                titleElem->font.setPointSize(12);
                titleElem->font.setBold(true);
                addElement(std::move(titleElem));

                auto artistElem = std::make_unique<TextElement>();
                artistElem->text = QString::fromStdString(first.artist);
                artistElem->rect = QRectF(5, 105, 110, 12);
                addElement(std::move(artistElem));

            } else if (m_template == TemplateType::JewelCaseBack) {
                // Album info on left, track list on right
                auto titleElem = std::make_unique<TextElement>();
                titleElem->text = QString::fromStdString(first.album);
                titleElem->rect = QRectF(5, 5, 60, 15);
                addElement(std::move(titleElem));

                // Track list on back panel (right side)
                auto trackElem = std::make_unique<TrackListElement>();
                trackElem->setFromLibraryQuery(tracks);
                trackElem->rect = QRectF(80, 10, 65, 100);  // Right panel
                addElement(std::move(trackElem));
            }
        }

        // Attempt to load cover art from library cache
        QString coverPath = m_pendingLibrary->coverArtPath(tracks[0].album);  // Assumes method exists
        if (!coverPath.isEmpty() && (m_template == TemplateType::JewelCaseFront ||
            m_template == TemplateType::DVDCaseCover)) {
            auto img = std::make_unique<ImageElement>();
        img->source = QUrl::fromLocalFile(coverPath);
        img->rect = QRectF(10, 10, 100, 100);
        img->maintainAspect = true;
        // Insert at beginning (background layer)
        m_elements.insert(m_elements.begin(), std::move(img));
            }

            emit libraryQueryFinished(true);
            emit elementsChanged();
            markModified();
    }

    // === DISC INTEGRATION ===

    void DiscLabelMaker::autoFillFromDisc(Disc* disc) {
        if (!disc) return;

        // Disconnect previous
        if (m_discConnection) {
            QObject::disconnect(m_discConnection);
        }

        // Connect to disc scan completion
        m_discConnection = connect(disc, &Disc::discChanged, [this, disc]() {
            onDiscScanned();
        });

        // Trigger scan if needed
        if (disc->discInfo().discId.isEmpty()) {
            disc->scanDisc();
        } else {
            onDiscScanned();
        }
    }

    void DiscLabelMaker::onDiscScanned() {
        // Get disc from sender or cache
        auto* disc = qobject_cast<Disc*>(sender());
        if (!disc) return;

        auto info = disc->discInfo();
        clearElements();
        auto geom = geometry();

        // Fill with CD-TEXT or MusicBrainz data
        QString title = info.title.isEmpty() ? "Unknown Album" : info.title;
        QString artist = info.artist.isEmpty() ? "Unknown Artist" : info.artist;

        if (m_template == TemplateType::Disc120mm) {
            auto t = std::make_unique<TextElement>();
            t->text = title;
            t->circularText = true;
            t->rect = QRectF(10, 10, geom.size.width() - 20, 30);
            addElement(std::move(t));
        }

        // Track listing from disc info
        if (m_template == TemplateType::JewelCaseBack && !info.tracks.isEmpty()) {
            auto te = std::make_unique<TrackListElement>();
            for (const auto& trk : info.tracks) {
                te->tracks.append(trk.title);
                te->durations.append(trk.duration);
            }
            te->rect = QRectF(80, 10, 65, 100);
            addElement(std::move(te));
        }

        emit autoFillCompleted("disc");
        markModified();
    }

    // === DISCBURNER INTEGRATION ===

    void DiscLabelMaker::attachToBurner(CDBurner* burner) {
        if (!burner) return;

        if (m_burnerConnection) {
            QObject::disconnect(m_burnerConnection);
        }

        m_burnerConnection = connect(burner, &CDBurner::burnFinished,
                                     this, &DiscLabelMaker::onBurnFinished);
    }

    void DiscLabelMaker::onBurnFinished(bool success, const BurnJob& job) {
        if (!success) return;

        // Prepare label for the disc that was just burned
        prepareForBurnJob(job);
        emit labelPreparedForBurn(job);
    }

    void DiscLabelMaker::prepareForBurnJob(const BurnJob& job) {
        // Determine template from burn type
        switch (job.type) {
            case BurnType::AudioCD:
            case BurnType::DataCD:
                setCurrentTemplate(TemplateType::Disc120mm);
                break;
            case BurnType::DVDVideo:
            case BurnType::DVDData:
                setCurrentTemplate(TemplateType::DVDCaseCover);
                break;
            case BurnType::BluRayData:
            case BurnType::BluRayVideo:
                setCurrentTemplate(TemplateType::BluRayCaseCover);
                break;
            case BurnType::ISO:
                // Keep current or default to CD
                if (m_template == TemplateType::CustomSize)
                    setCurrentTemplate(TemplateType::Disc120mm);
            break;
        }

        clearElements();
        auto geom = geometry();

        // Fill with burn job metadata
        if (!job.albumTitle.isEmpty()) {
            auto t = std::make_unique<TextElement>();
            t->text = job.albumTitle;
            if (geom.isDisc()) {
                t->circularText = true;
                t->rect = QRectF(10, 10, geom.size.width() - 20, 30);
            } else {
                t->rect = QRectF(10, 10, geom.size.width() - 20, 20);
                t->font.setBold(true);
            }
            addElement(std::move(t));
        }

        if (!job.artist.isEmpty()) {
            auto a = std::make_unique<TextElement>();
            a->text = job.artist;
            if (geom.isDisc()) {
                a->circularText = true;
                a->rect = QRectF(10, geom.size.height() - 40, geom.size.width() - 20, 20);
            } else {
                a->rect = QRectF(10, 35, geom.size.width() - 20, 15);
            }
            addElement(std::move(a));
        }

        // Fill track list if burning audio
        if (job.type == BurnType::AudioCD && !job.tracks.isEmpty()) {
            auto tl = std::make_unique<TrackListElement>();
            for (const auto& t : job.tracks) {
                tl->tracks.append(t.title);
                tl->durations.append(t.duration);
            }

            if (geom.isDisc()) {
                tl->rect = QRectF(35, 35, 50, 50);  // Center area of disc
            } else {
                tl->rect = QRectF(10, 60, geom.size.width() - 20, 80);
            }
            addElement(std::move(tl));
        }

        // Add "Burned on [Date]" text
        auto date = std::make_unique<TextElement>();
        date->text = QDate::currentDate().toString("yyyy-MM-dd");
        date->font.setPointSize(7);
        date->color = Qt::gray;
        if (geom.isDisc()) {
            date->rect = QRectF(40, 40, 40, 10);  // Small text near center
            date->circularText = true;
            date->circleRadius = 25;
        } else {
            date->rect = QRectF(10, geom.size.height() - 15, 100, 10);
        }
        addElement(std::move(date));

        markModified();
        emit elementsChanged();
    }

    // === ELEMENT MANAGEMENT ===

    void DiscLabelMaker::addElement(std::unique_ptr<LabelElement> elem) {
        if (elem->id.isEmpty()) {
            elem->id = QString("elem_%1").arg(m_nextId++);
        }
        m_elements.push_back(std::move(elem));
        emit elementsChanged();
    }

    LabelElement* DiscLabelMaker::findElement(const QString& id) {
        auto it = std::find_if(m_elements.begin(), m_elements.end(),
                               [&id](const auto& e) { return e->id == id; });
        return (it != m_elements.end()) ? it->get() : nullptr;
    }

    QString DiscLabelMaker::addText(const QString& text, const QRectF& rectMm) {
        auto elem = std::make_unique<TextElement>();
        elem->text = text;
        elem->rect = rectMm;
        QString id = elem->id;
        addElement(std::move(elem));
        markModified();
        return id;
    }

    QString DiscLabelMaker::addImage(const QUrl& source, const QRectF& rectMm) {
        auto elem = std::make_unique<ImageElement>();
        elem->source = source;
        elem->rect = rectMm;
        QString id = elem->id;
        addElement(std::move(elem));
        markModified();
        return id;
    }

    QString DiscLabelMaker::addShape(int shapeType, const QRectF& rectMm) {
        auto elem = std::make_unique<ShapeElement>();
        elem->shapeType = static_cast<ShapeElement::ShapeType>(shapeType);
        elem->rect = rectMm;
        QString id = elem->id;
        addElement(std::move(elem));
        markModified();
        return id;
    }

    QString DiscLabelMaker::addTrackList(const QRectF& rectMm) {
        auto elem = std::make_unique<TrackListElement>();
        elem->rect = rectMm;
        QString id = elem->id;
        addElement(std::move(elem));
        markModified();
        return id;
    }

    void DiscLabelMaker::removeElement(const QString& id) {
        auto it = std::remove_if(m_elements.begin(), m_elements.end(),
                                 [&id](const auto& e) { return e->id == id; });
        if (it != m_elements.end()) {
            m_elements.erase(it, m_elements.end());
            emit elementsChanged();
            markModified();
        }
    }

    void DiscLabelMaker::clearElements() {
        m_elements.clear();
        m_nextId = 0;
        emit elementsChanged();
        markModified();
    }

    void DiscLabelMaker::updateElement(const QString& id, const QVariantMap& props) {
        auto* elem = findElement(id);
        if (!elem) return;

        elem->deserialize(props);
        emit elementUpdated(id);
        markModified();
    }

    void DiscLabelMaker::moveElement(const QString& id, const QPointF& newPosMm) {
        auto* elem = findElement(id);
        if (!elem) return;
        elem->rect.moveTopLeft(newPosMm);
        emit elementUpdated(id);
        markModified();
    }

    QVariantList DiscLabelMaker::elements() const {
        QVariantList list;
        for (const auto& e : m_elements) {
            list.append(e->serialize());
        }
        return list;
    }

    QVariantMap DiscLabelMaker::element(const QString& id) const {
        auto it = std::find_if(m_elements.begin(), m_elements.end(),
                               [&id](const auto& e) { return e->id == id; });
        return (it != m_elements.end()) ? (*it)->serialize() : QVariantMap();
    }

    // === RENDERING ===

    void DiscLabelMaker::renderToPainter(QPainter* painter, const QRectF& rect, bool forPrint) const {
        auto geom = geometry();
        int dpi = painter->device()->logicalDpiX();

        // Background
        painter->fillRect(rect, m_bgColor);

        if (!m_bgImage.isEmpty()) {
            QImage bg(m_bgImage.toLocalFile());
            if (!bg.isNull()) {
                painter->drawImage(rect, bg);
            }
        }

        // Draw guides (cut lines, folds) if not for final print
        if (!forPrint) {
            painter->setPen(QPen(QColor(180, 180, 180), 1, Qt::DashLine));
            if (geom.isDisc()) {
                // Outer circle
                painter->drawEllipse(rect.center(), rect.width()/2, rect.height()/2);
                // Inner hub
                if (geom.innerDiameter > 0) {
                    double ratio = geom.innerDiameter / geom.outerDiameter;
                    painter->drawEllipse(rect.center(),
                                         rect.width()*ratio/2,
                                         rect.height()*ratio/2);
                }
            } else if (geom.isWraparound) {
                // Spine fold lines
                double leftWidth = (rect.width() - mmToPixels(geom.spineWidth, dpi)) / 2;
                painter->drawLine(rect.x() + leftWidth, rect.y(),
                                  rect.x() + leftWidth, rect.y() + rect.height());
                painter->drawLine(rect.right() - leftWidth, rect.y(),
                                  rect.right() - leftWidth, rect.y() + rect.height());
            }
        }

        // Render elements
        for (const auto& elem : m_elements) {
            if (!elem->visible) continue;

            painter->save();
            painter->setOpacity(elem->opacity);

            // Convert mm to pixels for this element
            QRectF elemRect(mmToPixels(elem->rect.x(), dpi),
                            mmToPixels(elem->rect.y(), dpi),
                            mmToPixels(elem->rect.width(), dpi),
                            mmToPixels(elem->rect.height(), dpi));

            // Apply rotation around center
            QPointF center = elemRect.center();
            painter.translate(center);
            painter->rotate(elem->rotation);
            painter->translate(-center);

            elem->render(painter, elemRect);
            painter->restore();
        }
    }

    QImage DiscLabelMaker::render(int dpi) const {
        auto geom = geometry();
        int w = static_cast<int>(mmToPixels(geom.size.width(), dpi));
        int h = static_cast<int>(mmToPixels(geom.size.height(), dpi));

        QImage img(w, h, QImage::Format_ARGB32);
        img.fill(Qt::transparent);

        QPainter painter(&img);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        renderToPainter(&painter, QRectF(0, 0, w, h), false);

        return img;
    }

    QImage DiscLabelMaker::renderPreview(const QSize& pixelSize) const {
        auto geom = geometry();
        // Calculate DPI to fit in preview box with margins
        double scale = std::min(pixelSize.width() / geom.size.width(),
                                pixelSize.height() / geom.size.height()) * 0.9;
                                int dpi = static_cast<int>(scale * 25.4);
                                dpi = qBound(72, dpi, 150);  // Clamp for preview performance

                                QImage fullRes = render(dpi);
                                return fullRes.scaled(pixelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    bool DiscLabelMaker::saveAsImage(const QString& path, const QString& format, int dpi) {
        return render(dpi).save(path, format.toUtf8().constData(), 95);
    }

    bool DiscLabelMaker::exportToPDF(const QString& path) {
        QPdfWriter writer(path);
        auto geom = geometry();
        writer.setPageSize(QPageSize(geom.size, QPageSize::Millimeter));
        writer.setResolution(300);

        QPainter painter(&writer);
        QRectF targetRect(0, 0, writer.width(), writer.height());

        // White background for PDF
        painter.fillRect(targetRect, Qt::white);
        renderToPainter(&painter, targetRect, true);

        return true;
    }

    // === PRINTING ===

    void DiscLabelMaker::print() {
        QPrintDialog dialog(m_printer.get(), nullptr);
        if (dialog.exec() != QDialog::Accepted) return;

        emit printStarted();

        QPainter painter(m_printer.get());
        if (!painter.isActive()) {
            emit printFinished(false);
            return;
        }

        auto geom = geometry();
        int dpi = m_printer->resolution();

        // Center on page with calibration offset
        QRectF pageRect = painter.viewport();
        double mmToPx = dpi / 25.4;
        double w = geom.size.width() * mmToPx;
        double h = geom.size.height() * mmToPx;
        double x = (pageRect.width() - w) / 2.0 + (m_printOffset.x() * mmToPx);
        double y = (pageRect.height() - h) / 2.0 + (m_printOffset.y() * mmToPx);

        QRectF targetRect(x, y, w, h);

        // White background
        painter.fillRect(targetRect, Qt::white);
        renderToPainter(&painter, targetRect, true);

        emit printFinished(true);
    }

    void DiscLabelMaker::calibratePrinter() {
        QPrintDialog dialog(m_printer.get());
        if (dialog.exec() != QDialog::Accepted) return;

        QPainter painter(m_printer.get());
        QRect page = painter.viewport();

        painter.drawText(page, Qt::AlignCenter | Qt::TextWordWrap,
                         QObject::tr("Printer Calibration Sheet\n\n"
                         "1. Measure the distance from the page edge to the crosshair center\n"
                         "2. Enter the values in Preferences > Print Offset\n"
                         "3. Positive X moves right, Positive Y moves down"));

        // Draw crosshairs at corners
        QVector<QPoint> points = {
            QPoint(page.width()/4, page.height()/4),
            QPoint(3*page.width()/4, page.height()/4),
            QPoint(page.width()/4, 3*page.height()/4),
            QPoint(3*page.width()/4, 3*page.height()/4),
            QPoint(page.width()/2, page.height()/2)
        };

        for (const auto& pt : points) {
            painter.drawLine(pt.x() - 25, pt.y(), pt.x() + 25, pt.y());
            painter.drawLine(pt.x(), pt.y() - 25, pt.x(), pt.y() + 25);
            painter.drawEllipse(pt, 3, 3);
        }
    }

    // === SERIALIZATION ===

    QVariantMap LabelElement::serialize() const {
        QVariantMap m;
        m["id"] = id;
        m["type"] = static_cast<int>(type);
        m["x"] = rect.x();
        m["y"] = rect.y();
        m["width"] = rect.width();
        m["height"] = rect.height();
        m["rotation"] = rotation;
        m["opacity"] = opacity;
        m["locked"] = locked;
        m["visible"] = visible;
        return m;
    }

    void LabelElement::deserialize(const QVariantMap& map) {
        if (map.contains("x")) rect.setX(map["x"].toDouble());
        if (map.contains("y")) rect.setY(map["y"].toDouble());
        if (map.contains("width")) rect.setWidth(map["width"].toDouble());
        if (map.contains("height")) rect.setHeight(map["height"].toDouble());
        if (map.contains("rotation")) rotation = map["rotation"].toDouble();
        if (map.contains("opacity")) opacity = map["opacity"].toDouble();
        if (map.contains("locked")) locked = map["locked"].toBool();
        if (map.contains("visible")) visible = map["visible"].toBool();
    }

    bool DiscLabelMaker::saveProject(const QString& path) {
        QJsonObject root;
        root["version"] = "2.0";
        root["template"] = static_cast<int>(m_template);
        root["paperType"] = static_cast<int>(m_paper);
        root["printOffsetX"] = m_printOffset.x();
        root["printOffsetY"] = m_printOffset.y();
        root["bgColor"] = m_bgColor.name();
        root["bgImage"] = m_bgImage.toString();

        QJsonArray arr;
        for (const auto& e : m_elements) {
            arr.append(QJsonObject::fromVariantMap(e->serialize()));
        }
        root["elements"] = arr;

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        m_projectPath = path;
        m_modified = false;
        emit modifiedChanged();
        emit projectChanged();
        return true;
    }

    bool DiscLabelMaker::loadProject(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;

        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        if (root["version"].toString() != "2.0") {
            // Handle version migration...
        }

        m_template = static_cast<TemplateType>(root["template"].toInt());
        m_paper = static_cast<LabelSheetType>(root["paperType"].toInt());
        m_printOffset = QPointF(root["printOffsetX"].toDouble(), root["printOffsetY"].toDouble());
        m_bgColor = QColor(root["bgColor"].toString());
        m_bgImage = QUrl(root["bgImage"].toString());

        m_elements.clear();
        for (const auto& v : root["elements"].toArray()) {
            QJsonObject o = v.toObject();
            ElementType t = static_cast<ElementType>(o["type"].toInt());
            std::unique_ptr<LabelElement> elem;

            switch(t) {
                case ElementType::Text: elem = std::make_unique<TextElement>(); break;
                case ElementType::Image: elem = std::make_unique<ImageElement>(); break;
                case ElementType::Shape: elem = std::make_unique<ShapeElement>(); break;
                case ElementType::TrackList: elem = std::make_unique<TrackListElement>(); break;
                default: continue;
            }

            elem->deserialize(o.toVariantMap());
            m_elements.push_back(std::move(elem));
        }

        m_projectPath = path;
        m_modified = false;
        emit currentTemplateChanged();
        emit elementsChanged();
        emit modifiedChanged();
        emit projectChanged();
        return true;
    }

    void DiscLabelMaker::newProject() {
        m_elements.clear();
        m_nextId = 0;
        m_projectPath.clear();
        m_modified = false;
        emit elementsChanged();
        emit modifiedChanged();
        emit projectChanged();
    }

    void DiscLabelMaker::markModified() {
        if (!m_modified) {
            m_modified = true;
            emit modifiedChanged();
        }
    }

} // namespace Aegis
