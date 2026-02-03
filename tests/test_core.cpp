#include <QtTest>
#include "core.h"

using namespace Aegis;

class TestCore : public QObject {
    Q_OBJECT
private slots:
    void construct_core_with_null_library_is_safe() {
        std::shared_ptr<Library> lib; // nullptr is allowed for a smoke test
        Core core(lib);
        QCOMPARE(static_cast<int>(core.state()), static_cast<int>(PlaybackState::Stopped));
    }
};

QTEST_MAIN(TestCore)
#include "test_core.moc"
