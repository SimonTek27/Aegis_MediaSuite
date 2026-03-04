// tests/test_smoke.cpp
// Minimal QtTest-based smoke test to verify test infrastructure.

#include <QtTest/QtTest>

class TestSmoke : public QObject {
    Q_OBJECT

private slots:
    void testTrue() { QVERIFY(true); }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
