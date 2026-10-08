/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_batch_result_header.cpp
 *
 * Date: 2026-10-08
 *
 * Author: turnarond
 *
 * Description: 批量命令结果表头回归测试 — 列宽必须允许手动拖动（Interactive），
 *              且前三列带合理初始宽度；末列保持拉伸填充。
 */

#include <QtTest>
#include <QTreeWidget>
#include <QHeaderView>

#include "tools/TelnetTool/BatchResultTable.h"

class TstBatchResultHeader : public QObject {
    Q_OBJECT

private slots:
    void headerColumnsAreUserResizable();
};

void TstBatchResultHeader::headerColumnsAreUserResizable()
{
    QTreeWidget tree;
    tree.setHeaderLabels({QStringLiteral("IP"), QStringLiteral("状态"),
                          QStringLiteral("耗时(ms)"), QStringLiteral("最后输出")});
    configureBatchResultHeader(tree.header());

    QHeaderView* header = tree.header();
    for (int col = 0; col < 3; ++col) {
        QCOMPARE(header->sectionResizeMode(col),
                 static_cast<QHeaderView::ResizeMode>(QHeaderView::Interactive));
    }
    QVERIFY2(header->sectionSize(0) >= 100, "IP 列初始宽度过小");
    QVERIFY2(header->sectionSize(1) >= 80, "状态列初始宽度过小");
    QVERIFY2(header->sectionSize(2) >= 80, "耗时列初始宽度过小");
    QVERIFY(header->stretchLastSection());
}

QTEST_MAIN(TstBatchResultHeader)
#include "tst_batch_result_header.moc"
