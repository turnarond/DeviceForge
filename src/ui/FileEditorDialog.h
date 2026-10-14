/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FileEditorDialog.h
 * Date: 2026-10-14
 * Author: turnarond
 *
 * Description: F4 预览/编辑对话框（v2.12 文件管理增强）— 非模态大窗：
 *              顶栏（文件名/路径/只读横幅/编码下拉）+ Consolas 正文 + 底栏
 *              （状态/另存本地/关闭/保存并上传）。IO 编排（异步读、冲突检查、
 *              回传、刷新）全部在 FileBrowserPanel，本组件只呈现与输入。
 */

#pragma once
#include <QDialog>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class FileEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit FileEditorDialog(QWidget* parent = nullptr);

    // 每次打开/重解码都整窗重灌；fromBom/eol 在保存编码回字节时使用
    void bind(const QString& fileName, const QString& fullPath, const QString& text,
              const QStringList& codecs, const QString& codec, const QString& eol,
              bool fromBom, const QString& statusNote,
              bool readOnly, const QString& readOnlyReason);

    QString currentCodec() const;
    void setCodecSilently(const QString& codec);        // 面板拒绝切码时回滚下拉
    QByteArray currentBytes() const;                    // 按当前 codec/eol/BOM 编码回字节
    bool isDirty() const { return m_dirty; }
    void markSaved();
    void setBusy(bool busy);
    void setStatusNote(const QString& note);
    void setDiscardGuard(std::function<bool()> guard);  // 脏关闭确认（false=阻止关闭）

signals:
    void saveRequested();
    void codecChangedRequest(const QString& newCodec);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void doSaveAsLocal();

    QLabel* m_title = nullptr;
    QLabel* m_path = nullptr;
    QLabel* m_badge = nullptr;
    QComboBox* m_codec = nullptr;
    QPlainTextEdit* m_text = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QString m_eol = QStringLiteral("LF");
    bool m_fromBom = false;
    bool m_dirty = false;
    bool m_binding = false;          // bind 期间屏蔽 textChanged/combo 噪声
    std::function<bool()> m_discardGuard;
};
